# Disassembles DXBC shader blobs with Windows d3dcompiler_47 (D3DDisassemble, 0x80 = hex literals) -> <file>.asm next to each. Used by tools/ron/niagara_gpu.py.
param([string[]]$Files)
Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices;
public static class D3D {
  [DllImport("d3dcompiler_47.dll")] public static extern int D3DDisassemble(byte[] src, UIntPtr size, uint flags, string comments, out IntPtr blob);
  public static string Dis(byte[] b) {
    IntPtr blob; int hr = D3DDisassemble(b, (UIntPtr)b.Length, 0x80, null, out blob);
    if (hr != 0) return "ERROR hr=" + hr.ToString("x");
    // ID3DBlob vtable: QueryInterface, AddRef, Release, GetBufferPointer, GetBufferSize
    IntPtr vt = Marshal.ReadIntPtr(blob);
    var gp = Marshal.GetDelegateForFunctionPointer<GetPtr>(Marshal.ReadIntPtr(vt, 3 * IntPtr.Size));
    var gs = Marshal.GetDelegateForFunctionPointer<GetSize>(Marshal.ReadIntPtr(vt, 4 * IntPtr.Size));
    return Marshal.PtrToStringAnsi(gp(blob), (int)gs(blob));
  }
  [UnmanagedFunctionPointer(CallingConvention.StdCall)] public delegate IntPtr GetPtr(IntPtr self);
  [UnmanagedFunctionPointer(CallingConvention.StdCall)] public delegate UIntPtr GetSize(IntPtr self);
}
"@
foreach ($f in $Files) { $b = [IO.File]::ReadAllBytes($f); [IO.File]::WriteAllText(($f -replace '\.dxbc$', '.asm'), [D3D]::Dis($b)); Write-Output "$f done" }
