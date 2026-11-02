// Export the analyzed DMC3 program's static function, call, and string maps.
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.Set;

public class ExportDmc3Inventory extends GhidraScript {
    private PrintWriter writer(File file) throws Exception {
        file.getParentFile().mkdirs();
        return new PrintWriter(new BufferedWriter(new OutputStreamWriter(
            new FileOutputStream(file), StandardCharsets.UTF_8)));
    }

    private String csv(Object value) {
        if (value == null) return "";
        return "\"" + value.toString().replace("\"", "\"\"") + "\"";
    }

    private void decompile(Function function, File output) throws Exception {
        if (function == null) return;
        DecompInterface decompiler = new DecompInterface();
        decompiler.setOptions(new DecompileOptions());
        if (!decompiler.openProgram(currentProgram)) {
            throw new RuntimeException("Unable to open program in decompiler");
        }
        try {
            DecompileResults results = decompiler.decompileFunction(function, 120, monitor);
            String text = "/* " + function.getName() + " @ " + function.getEntryPoint() + " */\n";
            if (results != null && results.decompileCompleted() && results.getDecompiledFunction() != null) {
                text += results.getDecompiledFunction().getC();
            } else {
                text += "/* Decompilation failed: " + (results == null ? "no results" : results.getErrorMessage()) + " */\n";
            }
            try (PrintWriter out = writer(output)) { out.print(text); }
        } finally {
            decompiler.dispose();
        }
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) throw new IllegalArgumentException("Output directory required");
        File output = new File(args[0]);
        output.mkdirs();

        try (PrintWriter meta = writer(new File(output, "program.csv"))) {
            meta.println("key,value");
            meta.println(csv("program") + "," + csv(currentProgram.getName()));
            meta.println(csv("language") + "," + csv(currentProgram.getLanguageID()));
            meta.println(csv("compiler") + "," + csv(currentProgram.getCompilerSpec().getCompilerSpecID()));
            meta.println(csv("image_base") + "," + csv(currentProgram.getImageBase()));
            meta.println(csv("function_count") + "," + currentProgram.getFunctionManager().getFunctionCount());
            meta.println(csv("instruction_count") + "," + currentProgram.getListing().getNumInstructions());
        }

        try (PrintWriter functionsOut = writer(new File(output, "functions.csv"));
             PrintWriter callsOut = writer(new File(output, "calls.csv"))) {
            functionsOut.println("address,name,body_bytes,signature,thunk,external");
            callsOut.println("caller_address,caller_name,callee_address,callee_name");
            FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
            while (functions.hasNext() && !monitor.isCancelled()) {
                Function function = functions.next();
                functionsOut.println(csv(function.getEntryPoint()) + "," + csv(function.getName()) + "," +
                    function.getBody().getNumAddresses() + "," + csv(function.getSignature()) + "," +
                    function.isThunk() + "," + function.isExternal());
                Set<Function> callees = function.getCalledFunctions(monitor);
                for (Function callee : callees) {
                    callsOut.println(csv(function.getEntryPoint()) + "," + csv(function.getName()) + "," +
                        csv(callee.getEntryPoint()) + "," + csv(callee.getName()));
                }
            }
        }

        int stringCount = 0;
        try (PrintWriter stringsOut = writer(new File(output, "string_xrefs.csv"))) {
            stringsOut.println("string_address,reference_address,function_address,function_name,value");
            DataIterator dataItems = currentProgram.getListing().getDefinedData(true);
            while (dataItems.hasNext() && !monitor.isCancelled()) {
                Data data = dataItems.next();
                if (!data.hasStringValue()) continue;
                Object value = data.getValue();
                if (value == null) continue;
                stringCount++;
                ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(data.getMinAddress());
                if (!references.hasNext()) {
                    stringsOut.println(csv(data.getMinAddress()) + ",,,," + csv(value));
                }
                while (references.hasNext()) {
                    Reference reference = references.next();
                    Address from = reference.getFromAddress();
                    Function function = currentProgram.getFunctionManager().getFunctionContaining(from);
                    stringsOut.println(csv(data.getMinAddress()) + "," + csv(from) + "," +
                        csv(function == null ? null : function.getEntryPoint()) + "," +
                        csv(function == null ? null : function.getName()) + "," + csv(value));
                }
            }
        }

        Address entry = toAddr(0x00100008L);
        Function entryFunction = currentProgram.getFunctionManager().getFunctionAt(entry);
        if (entryFunction == null) entryFunction = currentProgram.getFunctionManager().getFunctionContaining(entry);
        decompile(entryFunction, new File(output, "decompiled/entry_00100008.c"));
        for (int i = 1; i < args.length; i++) {
            Address address = toAddr(Long.parseUnsignedLong(args[i].replaceFirst("^(0x|0X)", ""), 16));
            Function function = currentProgram.getFunctionManager().getFunctionAt(address);
            if (function == null) function = currentProgram.getFunctionManager().getFunctionContaining(address);
            if (function != null) {
                decompile(function, new File(output, "decompiled/" + function.getEntryPoint() + "_" +
                    function.getName().replaceAll("[^A-Za-z0-9_.-]", "_") + ".c"));
            }
        }
        try (PrintWriter summary = writer(new File(output, "export_summary.csv"))) {
            summary.println("key,value");
            summary.println(csv("defined_strings") + "," + stringCount);
            summary.println(csv("requested_extra_decompilations") + "," + (args.length - 1));
        }
        println("DMC3 inventory exported to " + output.getAbsolutePath());
    }
}
