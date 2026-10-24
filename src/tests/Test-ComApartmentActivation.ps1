#requires -Version 5.1
<#
.SYNOPSIS
    Compare registry-driven COM activation from STA and MTA threads.
.DESCRIPTION
    This probes CoCreateInstance and QueryInterface through the registered
    in-process COM server, not DllGetClassObject. It does not simulate Explorer.
#>
[CmdletBinding()]
param(
    [ValidateSet('Both', 'Apartment')]
    [string] $ExpectedRegistration
)

$source = @'
using System;
using System.Runtime.InteropServices;
using System.Threading;

public static class ComApartmentProbe
{
    [DllImport("ole32.dll", ExactSpelling = true)]
    private static extern int CoInitializeEx(IntPtr reserved, uint coinit);

    [DllImport("ole32.dll", ExactSpelling = true)]
    private static extern void CoUninitialize();

    [DllImport("ole32.dll", ExactSpelling = true)]
    private static extern int CoCreateInstance(
        ref Guid clsid, IntPtr outer, uint clsctx, ref Guid iid,
        out IntPtr instance);

    [DllImport("ole32.dll", ExactSpelling = true)]
    private static extern int CoGetApartmentType(out int type, out int qualifier);

    public sealed class Result
    {
        public string Model;
        public string ClassName;
        public string Initialize;
        public string Apartment;
        public string Activation;
        public string Interface;
        public string Query;
        public string Error;
    }

    public static Result Run(Guid classId, Guid interfaceId, string className,
                             ApartmentState apartment)
    {
        var result = new Result { Model = apartment.ToString(), ClassName = className };
        Exception exception = null;
        var thread = new Thread(() =>
        {
            bool initialized = false;
            IntPtr unknown = IntPtr.Zero;
            IntPtr selected = IntPtr.Zero;
            try
            {
                int init = CoInitializeEx(IntPtr.Zero,
                    apartment == ApartmentState.STA ? 2U : 0U);
                result.Initialize = Format(init);
                if (init < 0) return;
                initialized = true;
                int kind, qualifier;
                result.Apartment = Format(CoGetApartmentType(out kind, out qualifier))
                    + " type=" + kind + " qualifier=" + qualifier;
                var iid = new Guid("00000000-0000-0000-C000-000000000046");
                int hr = CoCreateInstance(ref classId, IntPtr.Zero, 1U,
                                          ref iid, out unknown);
                result.Activation = Format(hr);
                if (hr >= 0 && unknown != IntPtr.Zero)
                {
                    result.Interface = interfaceId.ToString("D");
                    result.Query = Format(Marshal.QueryInterface(unknown,
                        ref interfaceId, out selected));
                }
            }
            catch (Exception ex) { exception = ex; }
            finally
            {
                if (selected != IntPtr.Zero) Marshal.Release(selected);
                if (unknown != IntPtr.Zero) Marshal.Release(unknown);
                if (initialized) CoUninitialize();
            }
        });
        thread.IsBackground = true;
        thread.SetApartmentState(apartment);
        thread.Start();
        if (!thread.Join(30000))
            throw new TimeoutException("COM activation timed out for " + className
                + " (" + apartment + ")");
        if (exception != null) result.Error = exception.ToString();
        return result;
    }

    private static string Format(int hresult)
    {
        return "0x" + unchecked((uint)hresult).ToString("X8");
    }
}
'@

Add-Type -TypeDefinition $source -ErrorAction Stop
$cases = @(
    @{ Name = 'Context IShellExtInit'; CLSID = '4A26024B-5499-45BE-A23F-4A65E88888D0'; IID = '000214E8-0000-0000-C000-000000000046' }
    @{ Name = 'Context IContextMenu'; CLSID = '4A26024B-5499-45BE-A23F-4A65E88888D0'; IID = '000214E4-0000-0000-C000-000000000046' }
    @{ Name = 'Context IShellPropSheetExt'; CLSID = '4A26024B-5499-45BE-A23F-4A65E88888D0'; IID = '000214E9-0000-0000-C000-000000000046' }
    @{ Name = 'Overlay IShellIconOverlayIdentifier'; CLSID = 'FBAB8C58-59C9-4522-B7E8-1A6AA23B318C'; IID = '0C6C4200-C589-11D0-999A-00C04FD655E1' }
)
$classes = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
    [Microsoft.Win32.RegistryHive]::ClassesRoot,
    [Microsoft.Win32.RegistryView]::Registry64)
try {
    foreach ($clsid in ($cases.CLSID | Select-Object -Unique)) {
        $key = $classes.OpenSubKey("CLSID\{$clsid}\InprocServer32")
        if ($null -eq $key) { throw "Missing registered server for $clsid" }
        try {
            $model = [string]$key.GetValue('ThreadingModel', '')
            if ($model -ine $ExpectedRegistration) {
                throw ${clsid}: expected $ExpectedRegistration, got $model"
            }
        }
        finally { $key.Dispose() }
    }
}
finally { $classes.Dispose() }

$results = @(
    foreach ($c in $cases) {
        foreach ($apartment in @(
            [System.Threading.ApartmentState]::STA,
            [System.Threading.ApartmentState]::MTA)) {
            [ComApartmentProbe]::Run(
                [Guid]$c.CLSID, [Guid]$c.IID, $c.Name, $apartment)
        }
    }
)
$results | Format-Table ClassName, Model, Initialize, Apartment, Activation, Query, Error -AutoSize | Out-Host
$results | ConvertTo-Json -Depth 4 | Write-Output

$success = '0x00000000'
$failures = @($results | Where-Object {
    $_.Error -or $_.Initialize -ne $success -or
    ($_.Model -eq 'STA' -and ($_.Activation -ne $success -or $_.Query -ne $success))
})
if ($failures.Count -gt 0) {
    throw "$($failures.Count) COM apartment probe(s) failed initialisation or STA activation"
}
# MTA activation/query results are observations, not pass/fail criteria.
# Apartment classes may require marshaling unavailable for shell interfaces.
