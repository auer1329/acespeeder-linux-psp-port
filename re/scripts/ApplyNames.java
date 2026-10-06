// Applies names from re/names.txt to functions and global labels.
// Line format: <hexaddr> <name> [type] [# comment]   (blank lines / '#' lines ignored)
// type: a data type name known to the program (e.g. from types.h), optionally with a
// trailing "[N]" for arrays; applied to global labels only.
// If a function starts at the address it is renamed, otherwise a primary label is set.
// Usage (headless): -preScript ApplyNames.java <names.txt>
//@category AceSpeeder

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;

import ghidra.program.model.data.ArrayDataType;
import ghidra.program.model.data.BuiltInDataTypeManager;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.PointerDataType;

import java.io.BufferedReader;
import java.util.ArrayList;
import java.util.List;
import java.io.FileReader;

public class ApplyNames extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String path = getScriptArgs()[0];
		SymbolTable st = currentProgram.getSymbolTable();
		int n = 0;
		try (BufferedReader r = new BufferedReader(new FileReader(path))) {
			String line;
			while ((line = r.readLine()) != null) {
				int hash = line.indexOf('#');
				if (hash >= 0) {
					line = line.substring(0, hash);
				}
				String[] parts = line.trim().split("\\s+");
				if (parts.length < 2) {
					continue;
				}
				Address a = toAddr(Long.parseLong(parts[0], 16));
				Function f = getFunctionAt(a);
				if (f == null && parts[1].startsWith("fn_")) {
					f = createFunction(a, null);
				}
				if (f != null) {
					f.setName(parts[1], SourceType.USER_DEFINED);
					if (parts[1].equals("__alloca_probe")) {
						f.setCallFixup("alloca_probe"); // MSVC stack probe: lets the decompiler track ESP
					}
				}
				else {
					Symbol s = st.createLabel(a, parts[1], SourceType.USER_DEFINED);
					s.setPrimary();
					if (parts.length >= 3) {
						applyType(a, parts[2]);
					}
				}
				n++;
			}
		}
		println("ApplyNames: applied " + n + " names");
	}

	private void applyType(Address a, String spec) throws Exception {
		int count = 1;
		String name = spec;
		if (spec.endsWith("]")) {
			int b = spec.indexOf('[');
			count = Integer.decode(spec.substring(b + 1, spec.length() - 1));
			name = spec.substring(0, b);
		}
		boolean ptr = name.endsWith("*");
		name = name.replace("*", "");
		List<DataType> found = new ArrayList<>();
		currentProgram.getDataTypeManager().findDataTypes(name, found);
		if (found.isEmpty()) {
			BuiltInDataTypeManager.getDataTypeManager().findDataTypes(name, found);
		}
		if (found.isEmpty()) {
			printerr("ApplyNames: unknown type " + spec);
			return;
		}
		DataType dt = found.get(0);
		if (ptr) {
			dt = new PointerDataType(dt);
		}
		if (count > 1) {
			dt = new ArrayDataType(dt, count, dt.getLength());
		}
		clearListing(a, a.add(dt.getLength() - 1));
		createData(a, dt);
	}
}
