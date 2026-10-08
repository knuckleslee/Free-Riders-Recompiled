# pmc-record.ps1: records a game's threads on this PC with the CPU's own
# counters (cycles and instructions at every context switch) and, with
# PresentMon, its frames, then writes a short summary that names no other
# program. Run it in an administrator PowerShell while the game is running:
#   powershell -ExecutionPolicy Bypass -File .\pmc-record.ps1
#   powershell -ExecutionPolicy Bypass -File .\pmc-record.ps1 -Process sfr_cpu_diagnostic.exe
param(
    [string]$Process = 'UnleashedRecomp.exe',
    [int]$Seconds = 60,
    [int]$Delay = 10,
    # PresentMon's console build (PresentMon-*-x64.exe); found next to this script or in X:\ when not given
    [string]$PresentMon = '',
    [string]$OutDir = (Join-Path $PSScriptRoot 'pmc-out'),
    [switch]$Keep
)
$ErrorActionPreference = 'Continue'  # native tools' stderr must not stop the script
$invariant = [Globalization.CultureInfo]::InvariantCulture

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw 'Run this from an administrator PowerShell.' }

$xperf = @(
    "${env:ProgramFiles(x86)}\Windows Kits\10\Windows Performance Toolkit\xperf.exe",
    "$env:ProgramFiles\Windows Kits\10\Windows Performance Toolkit\xperf.exe"
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $xperf) { throw 'xperf.exe not found: install the Windows Performance Toolkit from the Windows ADK.' }

if (-not $PresentMon) {
    $PresentMon = @(Get-ChildItem -Path $PSScriptRoot, 'X:\' -Filter 'PresentMon*.exe' -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName)[0]
}
if ($PresentMon -and -not (Test-Path -LiteralPath $PresentMon)) { throw "$PresentMon not found" }
if ($PresentMon) { Write-Host "PresentMon: $PresentMon" } else { Write-Host 'PresentMon not found: recording the CPU only (the fps can be typed at the end).' -ForegroundColor Yellow }

$name = [IO.Path]::GetFileNameWithoutExtension($Process)
if (-not (Get-Process -Name $name -ErrorAction SilentlyContinue)) {
    Write-Host "$Process is not running. Start the game and get into actual play first." -ForegroundColor Yellow
    exit 1
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$etl = Join-Path $OutDir "pmc-$stamp.etl"
$dump = Join-Path $OutDir "pmc-$stamp.txt"
$frames = Join-Path $OutDir "presentmon-$stamp.csv"
$summary = Join-Path $OutDir "pmc-$stamp-$name.md"

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;

public static class PmcSummary
{
    class ThreadStats
    {
        public int Pid; public int Tid; public string Module = "?";
        public double Microseconds; public ulong Cycles; public ulong Instructions;
        public long Slices; public long CountedSlices;
    }
    class Inside
    {
        public int Pid; public int Tid; public long Start; public ulong[] Counters;
    }

    const ulong Mask = (1UL << 48) - 1;

    static string ProcessName(string field, out int pid)
    {
        pid = -1;
        int open = field.LastIndexOf('(');
        int close = field.LastIndexOf(')');
        if (open < 0 || close < open) return field.Trim();
        int.TryParse(field.Substring(open + 1, close - open - 1).Trim(), out pid);
        return field.Substring(0, open).Trim();
    }

    static string[] Split(string line)
    {
        string[] parts = line.Split(',');
        for (int i = 0; i < parts.Length; ++i) parts[i] = parts[i].Trim();
        return parts;
    }

    // dump: the text of "xperf -i trace.etl -a dumper". process: the image name
    // (for example UnleashedRecomp.exe). fps: frames a second measured
    // elsewhere (PresentMon), 0 if unknown. Writes a short Markdown summary
    // that names no other process.
    public static string Run(string dump, string process, string output, double fps)
    {
        var threads = new Dictionary<long, ThreadStats>();
        var inside = new Dictionary<int, Inside>();
        var targetPids = new HashSet<int>();
        var pidFirst = new Dictionary<int, long>();
        var pidLast = new Dictionary<int, long>();
        long firstTs = -1, lastTs = -1, pmcLines = 0, switches = 0, missingPmc = 0;
        int cpus = 0;
        string[] lastPmc = null;
        string counterNames = "";

        using (var reader = new StreamReader(dump, Encoding.UTF8, true, 1 << 20))
        {
            string line;
            while ((line = reader.ReadLine()) != null)
            {
                string t = line.TrimStart();
                if (t.Length == 0) continue;
                if (t.StartsWith("Pmc,"))
                {
                    string[] p = Split(t);
                    if (p.Length > 1 && p[1] == "TimeStamp")
                    {
                        var names = new List<string>();
                        for (int i = 3; i < p.Length; ++i) names.Add(p[i]);
                        counterNames = string.Join(", ", names.ToArray());
                        continue;
                    }
                    lastPmc = p; ++pmcLines;
                    continue;
                }
                if (t.StartsWith("T-Start,") || t.StartsWith("T-DCStart,"))
                {
                    string[] p = Split(t);
                    if (p.Length < 15 || p[1] == "TimeStamp") continue;
                    int pid; string name = ProcessName(p[2], out pid);
                    if (!string.Equals(name, process, StringComparison.OrdinalIgnoreCase)) continue;
                    targetPids.Add(pid);
                    int tid; if (!int.TryParse(p[3], out tid)) continue;
                    ThreadStats s = Get(threads, pid, tid);
                    string start = p[14];
                    int bang = start.IndexOf('!');
                    s.Module = bang > 0 ? start.Substring(0, bang) : start;
                    continue;
                }
                if (t.StartsWith("CSwitch,"))
                {
                    string[] p = Split(t);
                    if (p.Length < 17 || p[1] == "TimeStamp") continue;
                    long ts; if (!long.TryParse(p[1], out ts)) continue;
                    if (firstTs < 0) firstTs = ts;
                    lastTs = ts;
                    ++switches;
                    int newPid, oldPid, newTid, oldTid, cpu;
                    string newName = ProcessName(p[2], out newPid);
                    ProcessName(p[8], out oldPid);
                    int.TryParse(p[3], out newTid); int.TryParse(p[9], out oldTid); int.TryParse(p[16], out cpu);
                    if (cpu + 1 > cpus) cpus = cpu + 1;
                    ulong[] snapshot = null;
                    if (lastPmc != null && lastPmc.Length > 3 && lastPmc[1] == p[1] && lastPmc[2] == p[3])
                    {
                        snapshot = new ulong[lastPmc.Length - 3];
                        for (int i = 0; i < snapshot.Length; ++i) ulong.TryParse(lastPmc[i + 3], out snapshot[i]);
                    }
                    else ++missingPmc;
                    lastPmc = null;
                    Inside was;
                    if (inside.TryGetValue(cpu, out was))
                    {
                        inside.Remove(cpu);
                        if (was.Tid == oldTid && was.Pid == oldPid)
                        {
                            pidLast[was.Pid] = ts;
                            ThreadStats s = Get(threads, was.Pid, was.Tid);
                            s.Microseconds += ts - was.Start;
                            ++s.Slices;
                            if (snapshot != null && was.Counters != null && snapshot.Length >= 2 && was.Counters.Length >= 2)
                            {
                                ulong cycles = (snapshot[0] - was.Counters[0]) & Mask;
                                ulong instructions = (snapshot[1] - was.Counters[1]) & Mask;
                                // A slice cannot run more cycles than 6 GHz allows: anything else is a wrapped or reset counter.
                                if (cycles <= (ulong)Math.Max(0, ts - was.Start) * 6000UL + 100000UL)
                                {
                                    s.Cycles += cycles; s.Instructions += instructions; ++s.CountedSlices;
                                }
                            }
                        }
                    }
                    if (string.Equals(newName, process, StringComparison.OrdinalIgnoreCase))
                    {
                        targetPids.Add(newPid);
                        if (!pidFirst.ContainsKey(newPid)) pidFirst[newPid] = ts;
                        pidLast[newPid] = ts;
                        var now = new Inside();
                        now.Pid = newPid; now.Tid = newTid; now.Start = ts; now.Counters = snapshot;
                        inside[cpu] = now;
                    }
                    continue;
                }
            }
        }

        var text = new StringBuilder();
        double seconds = firstTs >= 0 ? (lastTs - firstTs) / 1e6 : 0;
        text.AppendLine("# PMC summary: " + process);
        text.AppendLine();
        text.AppendLine("- Trace: " + F(seconds, 1) + " s, " + cpus + " logical processors, " + switches + " context switches, " +
                        pmcLines + " counter snapshots (" + counterNames + "), switches without a snapshot: " + missingPmc);
        foreach (int pid in targetPids)
        {
            var list = new List<ThreadStats>();
            double cpuMicroseconds = 0;
            foreach (var s in threads.Values)
                if (s.Pid == pid && s.Slices > 0) { list.Add(s); cpuMicroseconds += s.Microseconds; }
            list.Sort(delegate (ThreadStats a, ThreadStats b) { return b.Microseconds.CompareTo(a.Microseconds); });
            long first, last;
            double lifetime = pidFirst.TryGetValue(pid, out first) && pidLast.TryGetValue(pid, out last) ? (last - first) / 1e6 : seconds;
            if (lifetime <= 0) lifetime = seconds;
            double presents = fps * lifetime;
            text.AppendLine();
            text.AppendLine("## Process " + pid);
            text.AppendLine();
            text.AppendLine("- Running in the trace: " + F(lifetime, 1) + " s");
            text.AppendLine("- Frames a second used for the per-frame columns: " + (fps > 0 ? F(fps, 1) : "unknown"));
            text.AppendLine("- CPU used by all its threads: " + F(cpuMicroseconds / 1e6 / Math.Max(lifetime, 1e-9), 2) + " processors on average");
            text.AppendLine();
            text.AppendLine("| Thread | Started in | CPU ms/s | " + (presents > 0 ? "CPU ms/frame | M instructions/frame | " : "") +
                            "GHz | IPC | Slices/s | Slices with counters |");
            text.AppendLine("| ---: | --- | ---: | " + (presents > 0 ? "---: | ---: | " : "") + "---: | ---: | ---: | ---: |");
            int shown = 0;
            foreach (var s in list)
            {
                if (shown++ >= 20) break;
                double ms = s.Microseconds / 1e3;
                string row = "| " + s.Tid + " | " + s.Module + " | " + F(ms / lifetime, 1) + " | ";
                if (presents > 0) row += F(ms / presents, 2) + " | " + F(s.Instructions / 1e6 / presents, 2) + " | ";
                row += F(s.Microseconds > 0 ? s.Cycles / s.Microseconds / 1e3 : 0, 2) + " | " +
                       F(s.Cycles > 0 ? (double)s.Instructions / s.Cycles : 0, 2) + " | " +
                       F(s.Slices / lifetime, 0) + " | " + F(100.0 * s.CountedSlices / Math.Max(1, s.Slices), 0) + "% |";
                text.AppendLine(row);
            }
        }
        File.WriteAllText(output, text.ToString(), new UTF8Encoding(false));
        return text.ToString();
    }

    static ThreadStats Get(Dictionary<long, ThreadStats> threads, int pid, int tid)
    {
        long key = ((long)pid << 32) | (uint)tid;
        ThreadStats s;
        if (!threads.TryGetValue(key, out s)) { s = new ThreadStats(); s.Pid = pid; s.Tid = tid; threads[key] = s; }
        return s;
    }

    static string F(double value, int digits)
    {
        return value.ToString("F" + digits, CultureInfo.InvariantCulture);
    }
}

'@ -Language CSharp -ErrorAction Stop

# Leftovers of an earlier run would make -on fail.
& $xperf -stop sfrdxgi 2>$null | Out-Null
& $xperf -stop 2>$null | Out-Null

Write-Host ''
Write-Host "After Enter you have $Delay s to switch back to the game, then $Seconds s are recorded. Keep playing; stay out of menus and pause." -ForegroundColor Cyan
Read-Host 'Press Enter when ready' | Out-Null
for ($i = $Delay; $i -gt 0; --$i) { Write-Host "  recording in $i s"; Start-Sleep -Seconds 1 }

& $xperf -on PROC_THREAD+LOADER+CSWITCH -pmc UnhaltedCoreCyclesFixed,InstructionsRetiredFixed CSWITCH -BufferSize 1024 -MinBuffers 256 -MaxBuffers 2048
if ($LASTEXITCODE -ne 0) { throw "xperf -on failed ($LASTEXITCODE). Restart the PC and try again." }
$presentMonProcess = $null
if ($PresentMon) {
    $presentMonProcess = Start-Process -FilePath $PresentMon -PassThru -WindowStyle Minimized -ArgumentList @(
        '--process_name', $Process, '--timed', "$Seconds", '--terminate_after_timed', '--output_file', "`"$frames`"")
}

[console]::beep(880, 200)
for ($i = $Seconds; $i -gt 0; --$i) {
    Write-Progress -Activity 'Recording: keep playing' -SecondsRemaining $i -PercentComplete (100 * ($Seconds - $i) / $Seconds)
    Start-Sleep -Seconds 1
}
Write-Progress -Activity 'Recording' -Completed
& $xperf -d $etl
[console]::beep(660, 300)
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $etl)) { throw "Stopping the trace failed ($LASTEXITCODE)." }
if ($presentMonProcess -and -not $presentMonProcess.WaitForExit(30000)) { $presentMonProcess.Kill() }

# Frames from PresentMon: only this game's rows.
$fps = 0.0
$frameLines = @()
$rows = @()
if (Test-Path -LiteralPath $frames) { $rows = @(Import-Csv -LiteralPath $frames -Encoding UTF8 | Where-Object { $_.Application -eq $Process }) }
if ($rows.Count -gt 1) {
    $number = { param($text) $value = 0.0; if ([double]::TryParse($text, [Globalization.NumberStyles]::Float, $invariant, [ref]$value)) { $value } else { $null } }
    $span = ((& $number $rows[-1].TimeInMs) - (& $number $rows[0].TimeInMs)) / 1000
    if ($span -gt 0) { $fps = ($rows.Count - 1) / $span }
    $describe = {
        param($column)
        $values = @($rows | ForEach-Object { & $number $_.$column } | Where-Object { $_ -ne $null } | Sort-Object)
        if (-not $values.Count) { return 'NA' }
        $mean = ($values | Measure-Object -Average).Average
        '{0} / {1} / {2}' -f $mean.ToString('F2', $invariant), $values[[int]($values.Count / 2)].ToString('F2', $invariant),
            $values[[Math]::Min($values.Count - 1, [int]($values.Count * 0.95))].ToString('F2', $invariant)
    }
    $frameLines += '', '## Frames (PresentMon)', ''
    $frameLines += "- $($rows.Count) frames in $($span.ToString('F1', $invariant)) s: $($fps.ToString('F1', $invariant)) fps"
    $frameLines += "- Present: $((($rows | ForEach-Object { "$($_.PresentRuntime) $($_.PresentMode), sync interval $($_.SyncInterval)" }) | Sort-Object -Unique) -join '; ')"
    $frameLines += '', '| ms a frame (mean / median / P95) | |', '| --- | ---: |'
    foreach ($column in 'MsBetweenPresents', 'MsCPUBusy', 'MsCPUWait', 'MsGPUBusy', 'MsGPUWait', 'MsGPUTime', 'MsInPresentAPI') {
        $frameLines += "| $column | $(& $describe $column) |"
    }
} else {
    $typed = Read-Host 'PresentMon recorded no frames. If the game showed its fps, type the average (or just press Enter)'
    $value = 0.0
    if ($typed -and [double]::TryParse($typed, [Globalization.NumberStyles]::Float, $invariant, [ref]$value)) { $fps = $value }
    if ($fps -gt 0) { $frameLines += '', "- FPS seen in the game (typed): $typed" }
}

Write-Host 'Converting and analysing the trace; this can take a few minutes...'
& $xperf -i $etl -o $dump -a dumper
if ($LASTEXITCODE -ne 0) { throw "xperf -i failed ($LASTEXITCODE)." }
[PmcSummary]::Run($dump, $Process, $summary, $fps) | Out-Null

$draws = Read-Host 'Draw calls in a frame counted with RenderDoc, if any (or just press Enter)'
$cpu = (Get-CimInstance Win32_Processor | Select-Object -First 1)
$extra = $frameLines + @('', '## This PC', '', "- CPU: $($cpu.Name.Trim()) ($($cpu.NumberOfCores) cores, $($cpu.NumberOfLogicalProcessors) threads)")
if ($draws) { $extra += "- Draw calls a frame (RenderDoc): $draws" }
Add-Content -LiteralPath $summary -Value $extra -Encoding UTF8

if (-not $Keep) { Remove-Item -LiteralPath $etl, $dump, $frames -ErrorAction SilentlyContinue }
Write-Host ''
Get-Content -LiteralPath $summary | Write-Host
Write-Host ''
Write-Host "Summary: $summary" -ForegroundColor Green
Write-Host 'Send this .md file: it has this game's own threads and frames only, and names no other program.'
