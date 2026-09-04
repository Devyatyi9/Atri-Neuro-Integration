[CmdletBinding()]
param(
    # How long to hold the key down.
    [double]$Seconds = 10,
    # Process name of the game, without extension.
    [string]$ProcessName = "ATRI-MyDearMoments-"
)

# Holds the real Ctrl key so the game enters forced skip.
#
# For merely driving skip, prefer Tools/tjs/force-skip-on.tjs -- it needs no
# focus and no keyboard. This script exists for the one thing the TJS pair
# cannot do: act as a control. That pair works by stubbing skipKeyPressing, so
# any measurement taken under it is a measurement of a patched engine; running
# the same measurement here, through genuine input, is what shows the patch did
# not distort the result. (Used exactly so on 2026-09-03 to confirm that no SE
# plays during forced skip.)
#
# Force skip is the Ctrl hotkey, and the engine tests it with getKeyState
# (system/MainWindow.tjs, skipKeyPressing), which reads real keyboard state.
# Posting WM_KEYDOWN to the window therefore does nothing: the key has to be
# held for real, and the game has to be the foreground window.
#
# The engine also cancels SKIP_FAST the moment the key reads as released
# (checkCancelSkipFast), so the hold has to last the whole run -- calling
# kag.skipToStop2() alone drops back to SKIP_NONE within seconds.
#
# Consequences worth knowing before running this: it takes the foreground away
# from whatever you were doing (the window visibly flashes), and it presses a
# real Ctrl for the whole interval, so every other application sees the modifier
# held too. Keep the interval short and do not type elsewhere while it runs.

$ErrorActionPreference = "Stop"

Add-Type -Namespace AtriInput -Name Native -MemberDefinition @'
    [DllImport("user32.dll")]
    public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, System.UIntPtr dwExtraInfo);
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(System.IntPtr hWnd);
    [DllImport("user32.dll")]
    public static extern bool ShowWindow(System.IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")]
    public static extern System.IntPtr GetForegroundWindow();
    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(System.IntPtr hWnd, System.IntPtr lpdwProcessId);
    [DllImport("user32.dll")]
    public static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool fAttach);
    [DllImport("kernel32.dll")]
    public static extern uint GetCurrentThreadId();
'@

$VK_CONTROL = 0x11
$KEYEVENTF_KEYUP = 0x2
$SW_RESTORE = 9

try {
    $game = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowHandle -ne 0 } |
        Select-Object -First 1

    if (-not $game) {
        throw "Game process '$ProcessName' with a window was not found."
    }

    $handle = $game.MainWindowHandle
    [void][AtriInput.Native]::ShowWindow($handle, $SW_RESTORE)

    # Windows refuses SetForegroundWindow from a process that does not already
    # own the foreground. Attaching to the foreground window's input thread
    # lifts that restriction for the duration of the call.
    $foreground = [AtriInput.Native]::GetForegroundWindow()
    $targetThread = [AtriInput.Native]::GetWindowThreadProcessId($handle, [System.IntPtr]::Zero)
    $foregroundThread = [AtriInput.Native]::GetWindowThreadProcessId($foreground, [System.IntPtr]::Zero)
    $selfThread = [AtriInput.Native]::GetCurrentThreadId()

    $attached = $false
    if ($foregroundThread -ne 0 -and $foregroundThread -ne $selfThread) {
        $attached = [AtriInput.Native]::AttachThreadInput($selfThread, $foregroundThread, $true)
    }
    [void][AtriInput.Native]::SetForegroundWindow($handle)
    if ($attached) {
        [void][AtriInput.Native]::AttachThreadInput($selfThread, $foregroundThread, $false)
    }
    Start-Sleep -Milliseconds 300

    if ([AtriInput.Native]::GetForegroundWindow() -ne $handle) {
        Write-Warning "The game did not come to the foreground; the hold may not register."
    }

    Write-Host "Holding Ctrl for $Seconds s on '$ProcessName'..."
    [AtriInput.Native]::keybd_event([byte]$VK_CONTROL, 0, 0, [System.UIntPtr]::Zero)
    Start-Sleep -Seconds $Seconds
} finally {
    # Always release, including on Ctrl+C: a stuck modifier key affects the
    # whole desktop, not just the game.
    [AtriInput.Native]::keybd_event([byte]$VK_CONTROL, 0, $KEYEVENTF_KEYUP, [System.UIntPtr]::Zero)
    Write-Host "Ctrl released."
}
