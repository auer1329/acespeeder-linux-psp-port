// Exports every function's decompiled C to <outdir>/<addr>_<name>.c plus an index.
// Usage (headless): -postScript ExportDecomp.java <outdir>
//@category AceSpeeder

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

import java.io.File;
import java.io.PrintWriter;

public class ExportDecomp extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		File outDir = new File(args.length > 0 ? args[0] : "decomp");
		outDir.mkdirs();

		DecompInterface ifc = new DecompInterface();
		ifc.openProgram(currentProgram);

		try (PrintWriter index = new PrintWriter(new File(outDir, "INDEX.txt"))) {
			FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
			while (it.hasNext() && !monitor.isCancelled()) {
				Function f = it.next();
				String addr = f.getEntryPoint().toString();
				index.printf("%s %6d %s%s%n", addr, f.getBody().getNumAddresses(), f.getName(),
					f.isThunk() ? " [thunk]" : "");
				if (f.isThunk() || f.isExternal()) {
					continue;
				}
				DecompileResults res = ifc.decompileFunction(f, 60, monitor);
				String code = res.decompileCompleted()
						? res.getDecompiledFunction().getC()
						: "/* decompile failed: " + res.getErrorMessage() + " */\n";
				try (PrintWriter w = new PrintWriter(new File(outDir, addr + "_" + f.getName() + ".c"))) {
					w.print(code);
				}
			}
		}
		ifc.dispose();
	}
}
