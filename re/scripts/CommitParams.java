// Re-derives parameters/return of every non-user function from a fresh
// decompile and commits them (what the "Decompiler Parameter ID" analyzer does).
// Run after FixComCalls so stack-purge information is correct.
//@category AceSpeeder

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.pcode.HighFunctionDBUtil;
import ghidra.program.model.symbol.SourceType;

public class CommitParams extends GhidraScript {
	@Override
	protected void run() throws Exception {
		DecompInterface ifc = new DecompInterface();
		ifc.openProgram(currentProgram);
		int n = 0;
		for (int pass = 0; pass < 2; pass++) { // second pass sees callee signatures from the first
			for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
				if (f.isThunk() || f.isExternal() || f.getSignatureSource() == SourceType.USER_DEFINED
						|| f.getEntryPoint().getOffset() >= 0x43da00L) { // MSVC runtime: leave alone
					continue;
				}
				DecompileResults res = ifc.decompileFunction(f, 60, monitor);
				if (res.decompileCompleted()) {
					HighFunctionDBUtil.commitParamsToDatabase(res.getHighFunction(), true,
						HighFunctionDBUtil.ReturnCommitOption.COMMIT, SourceType.ANALYSIS);
					n++;
				}
			}
		}
		ifc.dispose();
		println("CommitParams: committed " + n);
	}
}
