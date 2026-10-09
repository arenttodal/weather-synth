# lldb helper for CI: when malloc reports a bad free, name the static data the pointer lands in.
# Usage: lldb --batch -o "command script import tools/lldb_badfree.py" -o "badfree_run" -- <host> <plugin>
import lldb

def badfree_run(debugger, command, result, internal_dict):
    target = debugger.GetSelectedTarget()
    target.BreakpointCreateByName("malloc_error_break")
    target.BreakpointCreateByName("abort")
    debugger.SetAsync(False)
    process = target.LaunchSimple(None, None, None)
    if process is None or process.GetState() != lldb.eStateStopped:
        print("process did not stop at a malloc error")
        return
    thread = process.GetSelectedThread()
    for i, frame in enumerate(thread.frames[:24]):
        print("#%d %s" % (i, frame))
    seen = set()
    for frame in thread.frames[:12]:
        for reg in ("x0", "x1", "x2", "x19", "x20", "x21"):
            v = frame.FindRegister(reg)
            if not v.IsValid():
                continue
            addr = v.GetValueAsUnsigned()
            if addr in seen or addr < 0x100000000:
                continue
            seen.add(addr)
            sa = target.ResolveLoadAddress(addr)
            if sa.GetModule().IsValid() and "Atmospheric" in str(sa.GetModule().GetFileSpec()):
                out = lldb.SBCommandReturnObject()
                debugger.GetCommandInterpreter().HandleCommand("image lookup -v -a 0x%x" % addr, out)
                print("frame %s %s=0x%x -> %s\n%s" % (frame.GetFunctionName(), reg, addr, sa.GetSection().GetName(), out.GetOutput()))
    process.Kill()

def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand("command script add -f lldb_badfree.badfree_run badfree_run")
