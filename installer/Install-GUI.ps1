$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System.Runtime.InteropServices;
public static class ScmDpi {
    [DllImport("user32.dll")]
    public static extern bool SetProcessDPIAware();
}
"@
[ScmDpi]::SetProcessDPIAware() | Out-Null

$encodedText = @(
    "U0NNIFY5LjEuMCBWOOWujOaVtOeVjOmdouWuieijheWZqA==",
    "5peg6ZyA5LiL6L295oiW57yW6K+R77yM6YCJ5oupIENvcmVsRFJBVyAyMDI0IOWuieijheS9jee9ruWQjuebtOaOpeWuieijheOAgg==",
    "6YCJ5oupIENvcmVsRFJBVyDlronoo4XkvY3nva4=",
    "5pm66IO95omr5o+P",
    "5omL5Yqo6YCJ5oup5paH5Lu25aS5",
    "5a6J6KOF5Yiw5omA6YCJ5L2N572u",
    "5Y246L295o+S5Lu2",
    "5YeG5aSH5bCx57uq44CC",
    "5q2j5Zyo5omr5o+PIENvcmVsRFJBVyA2NCDkvY3lronoo4XkvY3nva7igKbigKY=",
    "5bey5om+5YiwIHswfSDkuKogQ29yZWxEUkFXIOWuieijheS9jee9ru+8jOivt+mAieaLqeWQjuWuieijheOAgg==",
    "5rKh5pyJ6Ieq5Yqo5om+5YiwIENvcmVsRFJBV++8jOivt+eCueWHu+KAnOaJi+WKqOmAieaLqeaWh+S7tuWkueKAneOAgg==",
    "6K+36YCJ5oup5YyF5ZCrIENvcmVsRFJXLmV4ZSDnmoQgUHJvZ3JhbXM2NCDmlofku7blpLk=",
    "5omA6YCJ5paH5Lu25aS55Lit5rKh5pyJIENvcmVsRFJXLmV4ZeOAgg==",
    "6K+35YWI6YCJ5oup5LiA5LiqIENvcmVsRFJBVyDlronoo4XkvY3nva7jgII=",
    "6K+35YWI5a6M5YWo6YCA5Ye6IENvcmVsRFJBV++8jOeEtuWQjuWGjeWuieijheOAgg==",
    "5a6J6KOF5YyF5LiN5a6M5pW077ya5rKh5pyJ5om+5YiwIFNDTUF1dG9Db250b3VyTmF0aXZlNjQuY3Bn44CC",
    "5q2j5Zyo5Y246L295pen54mI5pys5LiO5pen6Z2i5p2/4oCm4oCm",
    "5q2j5Zyo5aSN5Yi2IDY0IOS9jeWOn+eUn+aPkuS7tuKApuKApg==",
    "5q2j5Zyo5qCh6aqM5a6J6KOF5paH5Lu24oCm4oCm",
    "5a6J6KOF5paH5Lu25qCh6aqM5aSx6LSl77yM6K+36YeN5paw6Kej5Y6L5a6J6KOF5YyF44CC",
    "5a6J6KOF5oiQ5Yqf",
    "U0NNIFY5LjEuMCDlt7LmiJDlip/lronoo4XjgIIKClY45a6M5pW05pON5L2c6Z2i5p2/5ZKMVjnljp/nlJ/lvJXmk47lnYflt7Llronoo4XliLDvvJp7MH0KCuaYr+WQpueri+WNs+WQr+WKqCBDb3JlbERSQVfvvJ8=",
    "5a6J6KOF5aSx6LSl",
    "5a6J6KOF5aSx6LSl77yaezB9",
    "5rKh5pyJ5qOA5rWL5Yiw5bey5a6J6KOF55qEIFNDTSBWOSDmj5Lku7bjgII=",
    "5Y246L295a6M5oiQ",
    "U0NNIFY5IOaPkuS7tuW3suWNuOi9veOAgumHjeaWsOWQr+WKqCBDb3JlbERSQVcg5ZCO55Sf5pWI44CC",
    "5q2j5Zyo5Y246L295o+S5Lu24oCm4oCm",
    "5YWz6Zet",
    "5bey6YCJ5oup77yaezB9",
    "5a6J6KOF5Zmo77yaVjkuMS4w772c5YaF572u5byV5pOO77yaVjkuMS4w772cNjQg5L2NIEMrKyDljp/nlJ/mj5Lku7bvvItWOOWujOaVtOaziuWdnumdouadv++9nOemu+e6v+eJiA==",
    "56Gu6K6k5Y246L29",
    "56Gu5a6a6KaB5Y246L295omA6YCJIENvcmVsRFJBVyDkuK3nmoQgU0NNIFY5IOaPkuS7tuWQl++8nw==",
    "5q2j5Zyo5ZCv5YqoIENvcmVsRFJBV+KApuKApg==",
    "5o+S5Lu25bey57uP5a6J6KOF5Yiw5omA6YCJ5L2N572u44CC",
    "5a6J6KOF5YyF5LiN5a6M5pW077ya5rKh5pyJ5om+5YiwIFY5IOaziuWdnumdouadv+aWh+S7tuOAgg==",
    "5q2j5Zyo5a6J6KOFIFY5IOWOn+eUn+W8leaTjuWSjOaziuWdnumdouadv+KApuKApg=="
)
function T([int]$index) {
    return [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($encodedText[$index]))
}

$script:corelLocations = New-Object System.Collections.Generic.List[string]
$script:corelLocationKeys = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
$script:selectedPrograms64 = $null
$sourceCpg = Join-Path $PSScriptRoot "SCMAutoContourNative64.cpg"
$sourcePanel = Join-Path $PSScriptRoot "SCMAutoContourV9Panel"

function Normalize-CorelPath([string]$candidate) {
    if ([string]::IsNullOrWhiteSpace($candidate)) { return $null }
    $normalized = [IO.Path]::GetFullPath($candidate.Trim()).TrimEnd("\")
    if ($normalized.Length -ge 2 -and $normalized[1] -eq ':') {
        $normalized = $normalized.Substring(0, 1).ToUpperInvariant() + $normalized.Substring(1)
    }
    return $normalized
}

function Add-CorelLocation([string]$candidate) {
    $candidate = Normalize-CorelPath $candidate
    if (-not $candidate) { return }
    if ((Test-Path (Join-Path $candidate "CorelDRW.exe")) -and
        $script:corelLocationKeys.Add($candidate)) {
        $script:corelLocations.Add($candidate)
    }
}

function Find-CorelLocations {
    $script:corelLocations.Clear()
    $script:corelLocationKeys.Clear()
    $registryKeys = @(
        "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\CorelDRW.exe",
        "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\App Paths\CorelDRW.exe"
    )
    foreach ($key in $registryKeys) {
        try {
            $exe = (Get-ItemProperty -LiteralPath $key -ErrorAction Stop).'(default)'
            if ($exe) { Add-CorelLocation (Split-Path -Parent $exe) }
        } catch {}
    }
    foreach ($drive in @("C", "D", "E", "F")) {
        foreach ($pattern in @(
            "${drive}:\Program Files\Corel\*\Programs64\CorelDRW.exe",
            "${drive}:\CorelDRAW\*\Programs64\CorelDRW.exe",
            "${drive}:\CorelDRAW\*\*\Programs64\CorelDRW.exe"
        )) {
            Get-Item $pattern -ErrorAction SilentlyContinue | ForEach-Object {
                Add-CorelLocation $_.Directory.FullName
            }
        }
    }
}

$form = New-Object System.Windows.Forms.Form
$form.Text = T 0
$form.ClientSize = New-Object System.Drawing.Size(760, 500)
$form.StartPosition = "CenterScreen"
$form.FormBorderStyle = "FixedDialog"
$form.MaximizeBox = $false
$form.BackColor = [Drawing.Color]::FromArgb(247, 249, 252)
$form.Font = New-Object Drawing.Font("Microsoft YaHei UI", 10)

$header = New-Object Windows.Forms.Panel
$header.Location = New-Object Drawing.Point(0, 0)
$header.Size = New-Object Drawing.Size(760, 92)
$header.BackColor = [Drawing.Color]::FromArgb(20, 105, 220)
$form.Controls.Add($header)

$logo = New-Object Windows.Forms.Label
$logo.Text = "SCM"
$logo.ForeColor = [Drawing.Color]::White
$logo.Font = New-Object Drawing.Font("Segoe UI", 18, [Drawing.FontStyle]::Bold)
$logo.Location = New-Object Drawing.Point(24, 19)
$logo.Size = New-Object Drawing.Size(82, 42)
$header.Controls.Add($logo)

$title = New-Object Windows.Forms.Label
$title.Text = T 0
$title.ForeColor = [Drawing.Color]::White
$title.Font = New-Object Drawing.Font("Microsoft YaHei UI", 16, [Drawing.FontStyle]::Bold)
$title.Location = New-Object Drawing.Point(105, 15)
$title.Size = New-Object Drawing.Size(610, 34)
$header.Controls.Add($title)

$subtitle = New-Object Windows.Forms.Label
$subtitle.Text = T 1
$subtitle.ForeColor = [Drawing.Color]::FromArgb(225, 238, 255)
$subtitle.Location = New-Object Drawing.Point(108, 54)
$subtitle.Size = New-Object Drawing.Size(610, 24)
$header.Controls.Add($subtitle)

$group = New-Object Windows.Forms.GroupBox
$group.Text = T 2
$group.Location = New-Object Drawing.Point(22, 108)
$group.Size = New-Object Drawing.Size(716, 185)
$group.BackColor = [Drawing.Color]::White
$form.Controls.Add($group)

$list = New-Object Windows.Forms.ListBox
$list.Location = New-Object Drawing.Point(16, 28)
$list.Size = New-Object Drawing.Size(682, 90)
$list.Font = New-Object Drawing.Font("Segoe UI", 10)
$group.Controls.Add($list)

$scanButton = New-Object Windows.Forms.Button
$scanButton.Text = T 3
$scanButton.Location = New-Object Drawing.Point(16, 132)
$scanButton.Size = New-Object Drawing.Size(135, 34)
$scanButton.FlatStyle = "Flat"
$scanButton.BackColor = [Drawing.Color]::FromArgb(235, 242, 252)
$group.Controls.Add($scanButton)

$manualButton = New-Object Windows.Forms.Button
$manualButton.Text = T 4
$manualButton.Location = New-Object Drawing.Point(163, 132)
$manualButton.Size = New-Object Drawing.Size(170, 34)
$manualButton.FlatStyle = "Flat"
$manualButton.BackColor = [Drawing.Color]::FromArgb(235, 242, 252)
$group.Controls.Add($manualButton)

$selectionLabel = New-Object Windows.Forms.Label
$selectionLabel.Text = T 7
$selectionLabel.Location = New-Object Drawing.Point(350, 139)
$selectionLabel.Size = New-Object Drawing.Size(344, 25)
$selectionLabel.ForeColor = [Drawing.Color]::FromArgb(70, 85, 105)
$group.Controls.Add($selectionLabel)

$statusPanel = New-Object Windows.Forms.Panel
$statusPanel.Location = New-Object Drawing.Point(22, 308)
$statusPanel.Size = New-Object Drawing.Size(716, 74)
$statusPanel.BackColor = [Drawing.Color]::White
$statusPanel.BorderStyle = "FixedSingle"
$form.Controls.Add($statusPanel)

$statusLabel = New-Object Windows.Forms.Label
$statusLabel.Text = T 7
$statusLabel.Location = New-Object Drawing.Point(14, 10)
$statusLabel.Size = New-Object Drawing.Size(685, 24)
$statusPanel.Controls.Add($statusLabel)

$progress = New-Object Windows.Forms.ProgressBar
$progress.Location = New-Object Drawing.Point(14, 42)
$progress.Size = New-Object Drawing.Size(685, 17)
$progress.Minimum = 0
$progress.Maximum = 100
$statusPanel.Controls.Add($progress)

$versionLabel = New-Object Windows.Forms.Label
$versionLabel.Text = T 30
$versionLabel.Location = New-Object Drawing.Point(24, 395)
$versionLabel.Size = New-Object Drawing.Size(500, 24)
$versionLabel.ForeColor = [Drawing.Color]::FromArgb(90, 100, 115)
$form.Controls.Add($versionLabel)

$installButton = New-Object Windows.Forms.Button
$installButton.Text = T 5
$installButton.Location = New-Object Drawing.Point(490, 430)
$installButton.Size = New-Object Drawing.Size(248, 48)
$installButton.FlatStyle = "Flat"
$installButton.FlatAppearance.BorderSize = 0
$installButton.BackColor = [Drawing.Color]::FromArgb(20, 105, 220)
$installButton.ForeColor = [Drawing.Color]::White
$installButton.Font = New-Object Drawing.Font("Microsoft YaHei UI", 12, [Drawing.FontStyle]::Bold)
$form.Controls.Add($installButton)

$uninstallButton = New-Object Windows.Forms.Button
$uninstallButton.Text = T 6
$uninstallButton.Location = New-Object Drawing.Point(22, 430)
$uninstallButton.Size = New-Object Drawing.Size(128, 48)
$uninstallButton.FlatStyle = "Flat"
$uninstallButton.BackColor = [Drawing.Color]::White
$form.Controls.Add($uninstallButton)

$closeButton = New-Object Windows.Forms.Button
$closeButton.Text = T 28
$closeButton.Location = New-Object Drawing.Point(162, 430)
$closeButton.Size = New-Object Drawing.Size(100, 48)
$closeButton.FlatStyle = "Flat"
$closeButton.BackColor = [Drawing.Color]::White
$form.Controls.Add($closeButton)

function Refresh-LocationList {
    $statusLabel.Text = T 8
    $progress.Value = 15
    $form.Refresh()
    Find-CorelLocations
    $list.Items.Clear()
    foreach ($location in $script:corelLocations) { [void]$list.Items.Add($location) }
    if ($list.Items.Count -gt 0) {
        $list.SelectedIndex = 0
        $script:selectedPrograms64 = [string]$list.SelectedItem
        $selectionLabel.Text = [string]::Format((T 29), $script:selectedPrograms64)
        $statusLabel.Text = [string]::Format((T 9), $list.Items.Count)
        $progress.Value = 35
    } else {
        $script:selectedPrograms64 = $null
        $statusLabel.Text = T 10
        $progress.Value = 0
    }
}

$list.Add_SelectedIndexChanged({
    if ($list.SelectedItem) {
        $script:selectedPrograms64 = [string]$list.SelectedItem
        $selectionLabel.Text = [string]::Format((T 29), $script:selectedPrograms64)
    }
})

$scanButton.Add_Click({ Refresh-LocationList })

$manualButton.Add_Click({
    $dialog = New-Object Windows.Forms.FolderBrowserDialog
    $dialog.Description = T 11
    if ($dialog.ShowDialog($form) -eq [Windows.Forms.DialogResult]::OK) {
        if (-not (Test-Path (Join-Path $dialog.SelectedPath "CorelDRW.exe"))) {
            [Windows.Forms.MessageBox]::Show($form, (T 12), (T 22), "OK", "Error") | Out-Null
            return
        }
        $normalized = Normalize-CorelPath $dialog.SelectedPath
        Add-CorelLocation $normalized
        $list.Items.Clear()
        foreach ($location in $script:corelLocations) { [void]$list.Items.Add($location) }
        $list.SelectedItem = $normalized
        $statusLabel.Text = [string]::Format((T 9), $list.Items.Count)
        $progress.Value = 35
    }
})

$installButton.Add_Click({
    try {
        if (-not $script:selectedPrograms64) { throw (T 13) }
        if (Get-Process -Name "CorelDRW" -ErrorAction SilentlyContinue) { throw (T 14) }
        if (-not (Test-Path $sourceCpg)) { throw (T 15) }
        if (-not (Test-Path (Join-Path $sourcePanel "AppUI.xslt"))) { throw (T 35) }
        $embeddedVersion = ([Diagnostics.FileVersionInfo]::GetVersionInfo($sourceCpg)).FileVersion
        if (-not $embeddedVersion -or -not $embeddedVersion.StartsWith("9.1.0")) {
            throw "安装包中的原生引擎不是 V9.1.0，已停止安装，防止旧版 CPG 被伪装成新版。"
        }

        $addons = Join-Path $script:selectedPrograms64 "Addons"
        $target = Join-Path $addons "SCMAutoContourV9"
        $targetPanel = Join-Path $addons "SCMAutoContourV9Panel"
        $targetCpg = Join-Path $target "SCMAutoContourNative64.cpg"
        $statusLabel.Text = T 16
        $progress.Value = 50
        $form.Refresh()
        foreach ($obsoleteName in @("AutoContourCN", "SCMAutoContourNative64", "SCMAutoContourV9", "SCMAutoContourV9Panel")) {
            $obsoletePath = Join-Path $addons $obsoleteName
            if (Test-Path -LiteralPath $obsoletePath) {
                Remove-Item -LiteralPath $obsoletePath -Recurse -Force
            }
        }

        $statusLabel.Text = T 36
        $progress.Value = 70
        $form.Refresh()
        New-Item -ItemType Directory -Force -Path $target | Out-Null
        Copy-Item $sourceCpg $targetCpg -Force
        Copy-Item -LiteralPath $sourcePanel -Destination $targetPanel -Recurse -Force
        "9.1.0-installer;9.1.0-engine;v8-full-panel" | Set-Content (Join-Path $target "VERSION") -Encoding ASCII

        $statusLabel.Text = T 18
        $progress.Value = 90
        $form.Refresh()
        $sourceHash = (Get-FileHash $sourceCpg -Algorithm SHA256).Hash
        $targetHash = (Get-FileHash $targetCpg -Algorithm SHA256).Hash
        if ($sourceHash -ne $targetHash) { throw (T 19) }
        foreach ($requiredPanelFile in @("CorelDrw.addon", "AppUI.xslt", "UserUI.xslt", "panel.html", "panel.css", "panel.js")) {
            if (-not (Test-Path (Join-Path $targetPanel $requiredPanelFile))) { throw (T 35) }
        }

        $progress.Value = 100
        $statusLabel.Text = T 34
        $answer = [Windows.Forms.MessageBox]::Show(
            $form, [string]::Format((T 21), $addons), (T 20), "YesNo", "Information")
        if ($answer -eq [Windows.Forms.DialogResult]::Yes) {
            $statusLabel.Text = T 33
            Start-Process (Join-Path $script:selectedPrograms64 "CorelDRW.exe")
        }
    } catch {
        $progress.Value = 0
        $statusLabel.Text = [string]::Format((T 23), $_.Exception.Message)
        [Windows.Forms.MessageBox]::Show(
            $form, [string]::Format((T 23), $_.Exception.Message), (T 22), "OK", "Error") | Out-Null
    }
})

$uninstallButton.Add_Click({
    try {
        if (-not $script:selectedPrograms64) { throw (T 13) }
        if (Get-Process -Name "CorelDRW" -ErrorAction SilentlyContinue) { throw (T 14) }
        $addons = Join-Path $script:selectedPrograms64 "Addons"
        $target = Join-Path $addons "SCMAutoContourV9"
        $targetPanel = Join-Path $addons "SCMAutoContourV9Panel"
        if (-not (Test-Path $target) -and -not (Test-Path $targetPanel)) { throw (T 24) }
        $answer = [Windows.Forms.MessageBox]::Show($form, (T 32), (T 31), "YesNo", "Warning")
        if ($answer -ne [Windows.Forms.DialogResult]::Yes) { return }
        $statusLabel.Text = T 27
        $progress.Value = 50
        if (Test-Path $target) { Remove-Item -LiteralPath $target -Recurse -Force }
        if (Test-Path $targetPanel) { Remove-Item -LiteralPath $targetPanel -Recurse -Force }
        $progress.Value = 100
        $statusLabel.Text = T 26
        [Windows.Forms.MessageBox]::Show($form, (T 26), (T 25), "OK", "Information") | Out-Null
    } catch {
        $progress.Value = 0
        $statusLabel.Text = [string]::Format((T 23), $_.Exception.Message)
        [Windows.Forms.MessageBox]::Show(
            $form, [string]::Format((T 23), $_.Exception.Message), (T 22), "OK", "Error") | Out-Null
    }
})

$closeButton.Add_Click({ $form.Close() })
$form.Add_Shown({ Refresh-LocationList })
[void]$form.ShowDialog()
