$ErrorActionPreference = "Stop"

$solutionRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $solutionRoot "x64\Debug"

$workerExe = Join-Path $buildDirectory "Worker.exe"
$userExe = Join-Path $buildDirectory "User.exe"

$clients = @()

# Checks whether a required executable exists.
function Assert-Executable {
    param([string]$Path)

    if (-not (Test-Path $Path -PathType Leaf)) {
        throw "Executable not found: $Path"
    }
}

# Starts a separate client process.
function Start-TestClient {
    param(
        [string]$Name,
        [string]$Executable,
        [string]$Arguments,
        [string[]]$ExpectedLines
    )

    $startInfo = New-Object System.Diagnostics.ProcessStartInfo

    $startInfo.FileName = $Executable
    $startInfo.Arguments = $Arguments
    $startInfo.WorkingDirectory = $buildDirectory

    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.CreateNoWindow = $true

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo

    if (-not $process.Start()) {
        throw "Could not start $Name"
    }

    return [PSCustomObject]@{
        Name = $Name
        Process = $process
        ExpectedLines = $ExpectedLines
        ErrorTask = $process.StandardError.ReadToEndAsync()
    }
}

# Waits until a client finishes sending its initial messages.
function Wait-ClientReady {
    param($Client)

    $outputLines = @()

    $deadline = [DateTime]::UtcNow.AddSeconds(10)

    while ($true) {
        $remaining = [int](
            $deadline - [DateTime]::UtcNow
        ).TotalMilliseconds

        if ($remaining -le 0) {
            throw "$($Client.Name): startup timed out"
        }

        $readTask = $Client.Process.StandardOutput.ReadLineAsync()

        if (-not $readTask.Wait($remaining)) {
            throw "$($Client.Name): output timed out"
        }

        $line = $readTask.Result

        if ($null -eq $line) {
            throw "$($Client.Name): exited before becoming ready"
        }

        $outputLines += $line

        if ($line -match "Press Enter to disconnect") {
            break
        }
    }

    $output = $outputLines -join "`n"

    foreach ($expected in $Client.ExpectedLines) {
        if (-not $output.Contains($expected)) {
            throw "$($Client.Name): missing output: $expected"
        }
    }

    if ($Client.Process.HasExited) {
        throw "$($Client.Name): terminated unexpectedly"
    }

    Write-Host "[PASS] $($Client.Name) is ready" `
        -ForegroundColor Green
}

# Closes a client by sending Enter to its standard input.
function Stop-TestClient {
    param($Client)

    $process = $Client.Process

    if ($process.HasExited) {
        throw "$($Client.Name): terminated unexpectedly"
    }

    $process.StandardInput.WriteLine("")
    $process.StandardInput.Flush()

    $outputTask = $process.StandardOutput.ReadToEndAsync()

    if (-not $process.WaitForExit(5000)) {
        throw "$($Client.Name): shutdown timed out"
    }

    $output = $outputTask.GetAwaiter().GetResult()
    $errors = $Client.ErrorTask.GetAwaiter().GetResult()

    if ($process.ExitCode -ne 0) {
        throw "$($Client.Name): exit code $($process.ExitCode). $errors"
    }

    if (-not $output.Contains("Disconnected from Load Balancer")) {
        throw "$($Client.Name): missing disconnection confirmation"
    }

    if (-not $output.Contains("stopped successfully")) {
        throw "$($Client.Name): missing shutdown confirmation"
    }

    Write-Host "[PASS] $($Client.Name) disconnected successfully" `
        -ForegroundColor Green
}

try {
    Write-Host "================================"
    Write-Host "      PROCESS SMOKE TEST"
    Write-Host "================================"
    Write-Host ""

    Assert-Executable $workerExe
    Assert-Executable $userExe

    # Verify that the Load Balancer is listening.
    $listener = Get-NetTCPConnection `
        -LocalPort 5059 `
        -State Listen `
        -ErrorAction SilentlyContinue

    if (-not $listener) {
        throw "Load Balancer is not listening on port 5059"
    }

    $serverProcess = Get-Process -Id @(
        $listener.OwningProcess | Select-Object -Unique
    )

    if (-not ($serverProcess.ProcessName -contains "LoadBalancer")) {
        throw "Port 5059 is not owned by LoadBalancer.exe"
    }

    Write-Host "[PASS] Load Balancer is listening" `
        -ForegroundColor Green

    # Start two separate workers.
    foreach ($workerId in @(1, 2)) {
        $client = Start-TestClient `
            -Name "Worker $workerId" `
            -Executable $workerExe `
            -Arguments "$workerId" `
            -ExpectedLines @(
                "Worker ID: $workerId",
                "Connected to Load Balancer",
                "REGISTER_WR sent successfully",
                "Registration bytes: 16"
            )

        $clients += $client

        Wait-ClientReady $client
    }

    # Start three separate users.
    foreach ($priority in @("high", "medium", "low")) {
        $priorityName = $priority.ToUpperInvariant()

        $client = Start-TestClient `
            -Name "User $priorityName" `
            -Executable $userExe `
            -Arguments $priority `
            -ExpectedLines @(
                "Connected to Load Balancer",
                "REGISTER_UR sent successfully",
                "Registration bytes: 8",
                "Priority: $priorityName",
                "REQUEST sent successfully",
                "Total request bytes: 22"
            )

        $clients += $client

        Wait-ClientReady $client
    }

    # Verify that all five clients remain active.
    foreach ($client in $clients) {
        if ($client.Process.HasExited) {
            throw "$($client.Name): is not running"
        }
    }

    Write-Host ""
    Write-Host "[PASS] All five client processes are active" `
        -ForegroundColor Green

    Write-Host ""
    Write-Host "Check the Load Balancer console:"
    Write-Host "  Registered workers: 2"
    Write-Host "  Registered users:   3"
    Write-Host "  Observed requests:  3"
    Write-Host ""

    $confirmation = Read-Host "Enter Y if the server output is correct"

    if ($confirmation -ne "Y") {
        throw "Server-side verification was not confirmed"
    }

    Write-Host "[PASS] Server-side registration confirmed" `
        -ForegroundColor Green

    # Disconnect all clients.
    Write-Host ""
    Write-Host "Disconnecting clients..."

    foreach ($client in $clients) {
        Stop-TestClient $client
    }

    Write-Host ""
    Write-Host "================================"
    Write-Host "All process smoke tests passed." `
        -ForegroundColor Green
    Write-Host "================================"
}
catch {
    Write-Host ""
    Write-Host "[FAIL] $($_.Exception.Message)" `
        -ForegroundColor Red

    exit 1
}
finally {
    # Release all processes created by the script.
    foreach ($client in $clients) {
        $process = $client.Process

        try {
            if (-not $process.HasExited) {
                $process.StandardInput.WriteLine("")
                $process.StandardInput.Flush()

                if (-not $process.WaitForExit(2000)) {
                    $process.Kill()
                    $process.WaitForExit()
                }
            }
        }
        catch {
            Write-Warning "Cleanup failed for $($client.Name)"
        }
        finally {
            $process.Dispose()
        }
    }
}