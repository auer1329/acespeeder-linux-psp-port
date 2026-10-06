// Parses re/types.h into the program's data types, then applies C prototypes
// from re/sigs.txt: one "<hexaddr> <prototype>;" per line. The function is also
// renamed to the prototype's name. Marked USER_DEFINED so CommitParams leaves them alone.
// Usage (headless): -postScript ApplySigs.java <sigs.txt> <types.h>
//@category AceSpeeder

import ghidra.app.cmd.function.ApplyFunctionSignatureCmd;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.cparser.C.CParser;
import ghidra.app.util.cparser.C.CParserUtils;
import ghidra.program.model.data.FunctionDefinitionDataType;
import ghidra.program.model.data.ParameterDefinition;
import ghidra.program.model.symbol.Namespace;
import java.util.Arrays;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

import java.io.BufferedReader;
import java.io.FileReader;
import java.nio.file.Files;
import java.nio.file.Path;

public class ApplySigs extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String types = Files.readString(Path.of(getScriptArgs()[1]));
		types = types.replaceAll("(?s)/\\*.*?\\*/", "").replaceAll("//[^\n]*", "");
		new CParser(currentProgram.getDataTypeManager(), true, null).parse(types);
		int n = 0;
		try (BufferedReader r = new BufferedReader(new FileReader(getScriptArgs()[0]))) {
			String line;
			while ((line = r.readLine()) != null) {
				line = line.trim();
				if (line.isEmpty() || line.startsWith("#")) {
					continue;
				}
				int sp = line.indexOf(' ');
				Function f = getFunctionAt(toAddr(Long.parseLong(line.substring(0, sp), 16)));
				String proto = line.substring(sp + 1).trim();
				boolean thiscall = proto.contains("__thiscall"); // the C parser rejects the keyword
				proto = proto.replace("__thiscall", "");
				if (f == null) {
					printerr("ApplySigs: no function for " + line);
					continue;
				}
				FunctionDefinitionDataType sig = CParserUtils.parseSignature(state.getTool() == null ? null
						: null, currentProgram, proto, true);
				if (sig == null) {
					printerr("ApplySigs: cannot parse " + proto);
					continue;
				}
				f.setName(sig.getName(), SourceType.USER_DEFINED);
				if (thiscall) {
					// First declared parameter is `this`: put the function in a class
					// namespace named after its pointee type so Ghidra types `this` itself.
					ParameterDefinition[] args = sig.getArguments();
					String cls = args[0].getDataType().getName().replace(" *", "").replace("*", "").trim();
					sig.setArguments(Arrays.copyOfRange(args, 1, args.length));
					Namespace ns = currentProgram.getSymbolTable().getNamespace(cls, null);
					if (ns == null) {
						ns = currentProgram.getSymbolTable().createClass(null, cls, SourceType.USER_DEFINED);
					}
					f.setParentNamespace(ns);
				}
				new ApplyFunctionSignatureCmd(f.getEntryPoint(), sig, SourceType.USER_DEFINED).applyTo(currentProgram);
				if (thiscall) {
					f.setCallingConvention("__thiscall");
				}
				n++;
			}
		}
		println("ApplySigs: applied " + n + " prototypes");
	}
}
