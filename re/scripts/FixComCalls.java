// Gives every indirect call through a vtable (`CALL [reg+off]`) a __stdcall
// signature override with the right number of stack arguments. Without this the
// decompiler does not know that COM methods pop their own arguments, and it
// loses track of the stack (producing in_stack_/unaff_retaddr garbage).
//
// The argument count comes from counting PUSHes before the call, checked against
// the counts the Wine headers allow at that vtable offset (re/com_nargs.txt).
// Usage (headless): -preScript FixComCalls.java <com_nargs.txt>
//@category AceSpeeder

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.pcode.HighFunctionDBUtil;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;

import java.io.BufferedReader;
import java.io.FileReader;
import java.util.*;

public class FixComCalls extends GhidraScript {
	private final Map<Integer, TreeSet<Integer>> cands = new HashMap<>();
	private final Map<Address, Integer> purged = new HashMap<>(); // call site -> slots popped by callee

	@Override
	protected void run() throws Exception {
		try (BufferedReader r = new BufferedReader(new FileReader(getScriptArgs()[0]))) {
			String line;
			while ((line = r.readLine()) != null) {
				String[] p = line.trim().split("\\s+");
				TreeSet<Integer> s = new TreeSet<>();
				for (int i = 1; i < p.length; i++) {
					s.add(Integer.parseInt(p[i]));
				}
				cands.put(Integer.parseInt(p[0]), s);
			}
		}

		int fixed = 0, odd = 0;
		for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
			saves = prologueSaves(f);
			for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
				if (!ins.getMnemonicString().equals("CALL") || !isVtableCall(ins)) {
					continue;
				}
				int off = vtableOffset(ins);
				Scan s = scanPushes(f, ins);
				TreeSet<Integer> c = cands.getOrDefault(off, new TreeSet<>());
				int n = s.count;
				if (!c.isEmpty() && !c.contains(n)) {
					Integer lower = c.lower(n);
					if (s.hitEntry && lower != null) {
						n = lower; // prologue register saves were counted as arguments
					}
					else {
						odd++;
						println(String.format("odd %s off=0x%x pushes=%d cands=%s", ins.getAddress(), off, n, c));
					}
				}
				if (n < 1) {
					n = 1;
				}
				purged.put(ins.getAddress(), n);
				HighFunctionDBUtil.writeOverride(f, ins.getAddress(), signature(n));
				fixed++;
			}
		}
		println("FixComCalls: overrode " + fixed + " call sites (" + odd + " not matching headers)");
	}

	private boolean isVtableCall(Instruction ins) {
		Object[] objs = ins.getOpObjects(0);
		boolean hasReg = false;
		for (Object o : objs) {
			if (o instanceof Register) {
				hasReg = true;
			}
		}
		// CALL dword ptr [reg + off] (memory operand). Exclude CALL reg.
		return hasReg && ins.getDefaultOperandRepresentation(0).contains("[");
	}

	private int vtableOffset(Instruction ins) {
		for (Object o : ins.getOpObjects(0)) {
			if (o instanceof Scalar) {
				return (int) ((Scalar) o).getSignedValue();
			}
		}
		return 0;
	}

	private Set<Address> saves = new HashSet<>();

	/** PUSHes of callee-saved registers that still hold their incoming value (prologue saves). */
	private Set<Address> prologueSaves(Function f) {
		Set<Address> out = new HashSet<>();
		Set<String> written = new HashSet<>();
		Instruction p = getInstructionAt(f.getEntryPoint());
		while (p != null && f.getBody().contains(p.getAddress())) {
			if (p.getFlowType().isJump() || p.getFlowType().isTerminal() || p.getFlowType().isCall()) {
				break;
			}
			String m = p.getMnemonicString();
			String op0 = p.getNumOperands() > 0 ? p.getDefaultOperandRepresentation(0) : "";
			if (m.equals("PUSH") && Set.of("EBX", "ESI", "EDI", "EBP").contains(op0)) {
				if (!written.contains(op0)) {
					out.add(p.getAddress());
				}
			}
			else if (p.getNumOperands() > 0 && !m.equals("PUSH") && !m.equals("CMP") && !m.equals("TEST")) {
				written.add(op0);
			}
			p = p.getNext();
		}
		return out;
	}

	private static class Scan {
		int count;
		boolean hitEntry;
	}

	private Scan scanPushes(Function f, Instruction call) {
		Scan s = new Scan();
		int need = 0; // pushes consumed by inner calls seen while walking backwards
		Instruction p = call.getPrevious();
		while (p != null && f.getBody().contains(p.getAddress())) {
			String m = p.getMnemonicString();
			if (p.getFlowType().isJump() || p.getFlowType().isTerminal()) {
				break;
			}
			if (m.equals("PUSH") && saves.contains(p.getAddress())) {
				s.hitEntry = true; // reached the prologue: everything earlier is register saves
				break;
			}
			if (m.equals("PUSH")) {
				if (need > 0) {
					need--;
				}
				else {
					s.count++;
				}
			}
			else if (m.equals("POP")) {
				need++;
			}
			else if (m.equals("CALL")) {
				Integer known = purged.get(p.getAddress());
				if (known == null) {
					Function callee = getFunctionAt(firstFlow(p));
					if (callee == null || callee.getStackPurgeSize() < 0) {
						break;
					}
					known = callee.getStackPurgeSize() / 4;
				}
				need += known;
			}
			else if (m.equals("ADD") && p.getDefaultOperandRepresentation(0).equals("ESP")) {
				Object[] o = p.getOpObjects(1);
				if (o.length == 1 && o[0] instanceof Scalar) {
					need += (int) ((Scalar) o[0]).getValue() / 4;
				}
				else {
					break;
				}
			}
			else if (writesEsp(p)) {
				break;
			}
			if (p.getAddress().equals(f.getEntryPoint())) {
				s.hitEntry = true;
				break;
			}
			if (isBranchTarget(p.getAddress())) {
				break;
			}
			p = p.getPrevious();
		}
		return s;
	}

	private Address firstFlow(Instruction call) {
		Address[] flows = call.getFlows();
		return flows.length > 0 ? flows[0] : call.getAddress();
	}

	private boolean writesEsp(Instruction p) {
		String m = p.getMnemonicString();
		if (m.equals("PUSH") || m.equals("POP") || m.equals("CALL")) {
			return false;
		}
		return p.getNumOperands() > 0 && p.getDefaultOperandRepresentation(0).equals("ESP");
	}

	private boolean isBranchTarget(Address a) {
		for (Reference r : getReferencesTo(a)) {
			if (r.getReferenceType().isFlow() && !r.getReferenceType().isFallthrough()) {
				return true;
			}
		}
		return false;
	}

	private FunctionDefinitionDataType signature(int nargs) throws Exception {
		FunctionDefinitionDataType sig = new FunctionDefinitionDataType("com_" + nargs);
		ParameterDefinition[] params = new ParameterDefinition[nargs];
		params[0] = new ParameterDefinitionImpl("this", new PointerDataType(new PointerDataType()), null);
		for (int i = 1; i < nargs; i++) {
			params[i] = new ParameterDefinitionImpl("a" + i, new Undefined4DataType(), null);
		}
		sig.setArguments(params);
		sig.setReturnType(new IntegerDataType());
		sig.setCallingConvention("__stdcall");
		return sig;
	}
}
