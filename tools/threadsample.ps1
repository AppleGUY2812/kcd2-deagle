# Sampling profiler for a running game, from outside (no admin, no injection): every -EveryMs it pauses each thread
# that used CPU since the last tick for a few microseconds, reads its instruction pointer and the top of its stack,
# and lets it go. One CSV row per sample: time, thread id, "module+offset" of the instruction, and up to 8 return
# addresses found on the stack inside the game's modules (a crude call stack: values on the stack that point into
# code, not a real unwind). WHGame.DLL+<offset> is address 0x180000000+<offset> in the Ghidra project (qk dec).
# Made 2026-10-23 for "why is the first save load slow": parts of the load run on one busy thread.
# usage: powershell -ExecutionPolicy Bypass -File tools/threadsample.ps1 -Seconds 60 -Out C:\path\samples.csv [-WaitStart]
param([int]$Seconds = 60, [int]$EveryMs = 10, [string]$Out = "samples.csv", [string]$Process = "KingdomCome",
      [switch]$WaitStart)
$src = @"
using System; using System.Runtime.InteropServices; using System.Text; using System.Collections.Generic; using System.Diagnostics; using System.IO;
public static class TS {
  [DllImport("kernel32")] static extern IntPtr OpenThread(uint a, bool b, int id);
  [DllImport("kernel32")] static extern bool CloseHandle(IntPtr h);
  [DllImport("kernel32")] static extern uint SuspendThread(IntPtr h);
  [DllImport("kernel32")] static extern uint ResumeThread(IntPtr h);
  [DllImport("kernel32")] static extern bool GetThreadContext(IntPtr h, IntPtr ctx);
  [DllImport("kernel32")] static extern bool GetThreadTimes(IntPtr h, out long c, out long e, out long k, out long u);
  [DllImport("kernel32")] static extern IntPtr OpenProcess(uint a, bool b, int pid);
  [DllImport("kernel32")] static extern bool ReadProcessMemory(IntPtr h, IntPtr a, byte[] buf, IntPtr n, out IntPtr r);
  struct Mod { public string Name; public long Base, End; }
  static List<Mod> mods = new List<Mod>();
  static string Where(long a) {
    foreach (var m in mods) if (a >= m.Base && a < m.End) return m.Name + "+" + (a - m.Base).ToString("x");
    return null;
  }
  public static string Run(int pid, int seconds, int everyMs, string outPath) {
    var p = Process.GetProcessById(pid);
    foreach (ProcessModule m in p.Modules) mods.Add(new Mod { Name = m.ModuleName, Base = (long)m.BaseAddress, End = (long)m.BaseAddress + m.ModuleMemorySize });
    IntPtr ph = OpenProcess(0x0410, false, pid);
    IntPtr ctx = Marshal.AllocHGlobal(0x4D0 + 16);
    IntPtr c = (IntPtr)(((long)ctx + 15) & ~15L);
    var handles = new Dictionary<int, IntPtr>(); var last = new Dictionary<int, long>();
    var stack = new byte[2048]; long samples = 0;
    var sw = Stopwatch.StartNew(); long nextRefresh = 0;
    using (var w = new StreamWriter(outPath)) {
      w.WriteLine("t_ms,tid,rip,stack");
      while (sw.ElapsedMilliseconds < seconds * 1000L) {
        if (sw.ElapsedMilliseconds >= nextRefresh) {   // new threads, once a second
          nextRefresh = sw.ElapsedMilliseconds + 1000;
          try { p.Refresh(); foreach (ProcessThread t in p.Threads) if (!handles.ContainsKey(t.Id)) {
            IntPtr h = OpenThread(0x0002 | 0x0008 | 0x0040, false, t.Id); if (h != IntPtr.Zero) { handles[t.Id] = h; last[t.Id] = -1; } } }
          catch { break; }
          if (p.HasExited) break;
        }
        long now = sw.ElapsedMilliseconds;
        foreach (var kv in handles) {
          long cr, ex, k, u; if (!GetThreadTimes(kv.Value, out cr, out ex, out k, out u)) continue;
          long cpu = k + u; long was = last[kv.Key]; last[kv.Key] = cpu;
          if (was < 0 || cpu == was) continue;      // only threads that ran since the last tick
          if (SuspendThread(kv.Value) == 0xFFFFFFFF) continue;
          Marshal.WriteInt32(c, 0x30, 0x100001);     // CONTEXT_CONTROL: Rip, Rsp
          bool ok = GetThreadContext(kv.Value, c);
          long rip = 0, rsp = 0; IntPtr got = IntPtr.Zero; bool okStack = false;
          if (ok) { rsp = Marshal.ReadInt64(c, 0x98); rip = Marshal.ReadInt64(c, 0xF8); okStack = ReadProcessMemory(ph, (IntPtr)rsp, stack, (IntPtr)stack.Length, out got); }
          ResumeThread(kv.Value);
          if (!ok) continue;
          var sb = new StringBuilder(); int found = 0;
          if (okStack) for (int i = 0; i + 8 <= (long)got && found < 8; i += 8) {
            string s = Where(BitConverter.ToInt64(stack, i)); if (s != null && s.StartsWith("WHGame")) { if (found > 0) sb.Append(' '); sb.Append(s); found++; } }
          w.WriteLine(now + "," + kv.Key + "," + (Where(rip) ?? ("?" + rip.ToString("x"))) + "," + sb.ToString());
          samples++;
        }
        long sleep = everyMs - (sw.ElapsedMilliseconds - now); if (sleep > 0) System.Threading.Thread.Sleep((int)sleep);
      }
    }
    foreach (var h in handles.Values) CloseHandle(h);
    return "samples " + samples + ", threads " + handles.Count;
  }
}
"@
Add-Type -TypeDefinition $src
if ($WaitStart) { while (-not (Get-Process $Process -ErrorAction SilentlyContinue)) { Start-Sleep -Milliseconds 500 } }
"start " + (Get-Date).ToString("HH:mm:ss.fff")
[TS]::Run((Get-Process $Process).Id, $Seconds, $EveryMs, $Out)
