// Types .rdata locations read by x87 instructions as float/double, so the
// decompiler (which respects read-only blocks) folds them into literals.
//@category AceSpeeder

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DoubleDataType;
import ghidra.program.model.data.FloatDataType;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;

public class TypeRdataConsts extends GhidraScript {
	@Override
	protected void run() throws Exception {
		MemoryBlock rdata = currentProgram.getMemory().getBlock(".rdata");
		int n = 0;
		InstructionIterator it = currentProgram.getListing().getInstructions(true);
		while (it.hasNext()) {
			Instruction ins = it.next();
			String text = ins.toString();
			if (!ins.getMnemonicString().startsWith("F")) {
				continue;
			}
			DataType dt;
			if (text.contains("qword ptr") || text.contains("double ptr")) {
				dt = new DoubleDataType();
			}
			else if (text.contains("dword ptr") || text.contains("float ptr")) {
				dt = new FloatDataType();
			}
			else {
				continue;
			}
			for (Reference ref : ins.getReferencesFrom()) {
				Address to = ref.getToAddress();
				if (!rdata.contains(to)) {
					continue;
				}
				Data d = getDataAt(to);
				if (d != null && d.getDataType().getName().equals(dt.getName())) {
					continue;
				}
				try {
					clearListing(to, to.add(dt.getLength() - 1));
					createData(to, dt);
					n++;
				}
				catch (Exception e) {
					println("skip " + to + ": " + e.getMessage());
				}
			}
		}
		println("TypeRdataConsts: typed " + n + " constants");
	}
}
