// Mark tail-call jumps (listed in tailcalls.txt) as CALL_RETURN so the jump
// target becomes its own function instead of part of the caller's body.
//@category AceSpeeder
import java.nio.file.*;
import java.util.*;

import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;

public class FixTailCalls extends GhidraScript {
	@Override
	protected void run() throws Exception {
		int n = 0;
		for (String line : Files.readAllLines(Paths.get(getScriptArgs()[0]))) {
			line = line.replaceAll("#.*", "").trim();
			if (line.isEmpty()) {
				continue;
			}
			Address a = toAddr(Long.parseLong(line.split("\\s+")[0], 16));
			Instruction ins = getInstructionAt(a);
			if (ins == null) {
				println("FixTailCalls: no instruction at " + a);
				continue;
			}
			ins.setFlowOverride(FlowOverride.CALL_RETURN);
			Function f = getFunctionContaining(a);
			if (f != null) {
				CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor);
			}
			for (Address t : ins.getFlows()) {
				if (getFunctionAt(t) == null) {
					createFunction(t, null);
				}
			}
			n++;
		}
		println("FixTailCalls: " + n + " jumps");
	}
}
