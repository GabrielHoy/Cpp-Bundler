#Requires -Version 7.0
<#
.SYNOPSIS
    Rewrites this template in place into a fresh project.

.DESCRIPTION
    Performs the rename checklist that would otherwise be manual: the CMake project name and
    version, the vcpkg manifest identity, the Win32 resource description, the LICENSE
    copyright line, and the README. Optionally strips the example sources, wipes generated
    build output, and reinitialises git history.

    Single-use by design: it deletes itself when finished unless -KeepScript is passed.

.PARAMETER ProjectName
    CMake project name and default executable name. Must be a valid C identifier, since it
    also becomes the target name. Example: MyCoolApp

.PARAMETER Description
    One-line description. Lands in vcpkg.json, the Win32 FileDescription resource, and the
    README subtitle.

.PARAMETER ExecutableName
    Output binary name, when it should differ from ProjectName.

.PARAMETER PackageName
    vcpkg manifest name. Must be lowercase kebab-case. Derived from ProjectName by default.

.PARAMETER Version
    Three-component semantic version. Defaults to 0.1.0.

.PARAMETER Author
    Copyright holder written into LICENSE. Defaults to the existing holder.

.PARAMETER KeepExample
    Keep src/app.* and tests/test_app.cpp instead of replacing them with stubs.

.PARAMETER ResetGit
    Delete .git and start a fresh repository with a single initial commit.
    Destructive and irreversible; always prompts unless -Force is also passed.

.PARAMETER KeepScript
    Do not delete this script when finished.

.PARAMETER Force
    Skip the confirmation prompt.

.EXAMPLE
    ./BOOTSTRAP_TEMPLATE.ps1 -ProjectName MyCoolApp -Description "Does a cool thing."

.EXAMPLE
    ./BOOTSTRAP_TEMPLATE.ps1 -ProjectName Renderer -Description "Toy path tracer." -WhatIf

    Shows every change without touching the working tree.

.EXAMPLE
    ./BOOTSTRAP_TEMPLATE.ps1 -ProjectName Renderer -Description "Toy path tracer." -ResetGit -Force
#>
[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(Mandatory, Position = 0)]
    [ValidatePattern('^[A-Za-z][A-Za-z0-9_]*$',
        ErrorMessage = "'{0}' is not a valid CMake target name. Use letters, digits and underscores, starting with a letter.")]
    [string]$ProjectName,

    [Parameter(Mandatory, Position = 1)]
    [ValidateNotNullOrEmpty()]
    [string]$Description,

    [ValidatePattern('^[A-Za-z][A-Za-z0-9_.\-]*$')]
    [string]$ExecutableName,

    # vcpkg rejects anything that isn't lowercase alphanumerics separated by single dashes.
    [ValidatePattern('^[a-z0-9]+(-[a-z0-9]+)*$',
        ErrorMessage = "'{0}' is not a valid vcpkg port name. Use lowercase kebab-case, e.g. my-cool-app.")]
    [string]$PackageName,

    [ValidatePattern('^\d+\.\d+\.\d+$',
        ErrorMessage = "'{0}' must be a three-component version, e.g. 0.1.0.")]
    [string]$Version = '0.1.0',

    [ValidateNotNullOrEmpty()]
    [string]$Author = 'Gabriel Hoy',

    [switch]$KeepExample,
    [switch]$ResetGit,
    [switch]$KeepScript,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Operate on the repository this script lives in, not on whatever the caller's cwd happens
# to be.
$Root = $PSScriptRoot

# Identity of the template as shipped. If these strings are already gone, bootstrapping has
# happened before and re-running would corrupt a real project.
$TemplateProject = 'CPPApplicationTemplate'
$TemplatePackage = 'cpp-application-template'

# ---------------------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------------------

# All writes go through here so encoding is consistent: UTF-8 without BOM, matching
# .editorconfig. Set-Content would append a trailing newline and Out-File defaults to BOM on
# some hosts, so neither is used.
$Utf8NoBom = [System.Text.UTF8Encoding]::new($false)

function Read-RepoFile {
    param([Parameter(Mandatory)][string]$RelativePath)

    $full = Join-Path $Root $RelativePath
    if (-not (Test-Path -LiteralPath $full -PathType Leaf)) {
        throw "Expected file is missing from the template: $RelativePath"
    }
    return [System.IO.File]::ReadAllText($full)
}

function Write-RepoFile {
    param(
        [Parameter(Mandatory)][string]$RelativePath,
        [Parameter(Mandatory)][AllowEmptyString()][string]$Content
    )

    $full = Join-Path $Root $RelativePath
    if ($PSCmdlet.ShouldProcess($RelativePath, 'Write')) {
        [System.IO.File]::WriteAllText($full, $Content, $Utf8NoBom)
    }
}

# Replaces a literal substring and fails loudly if it wasn't found. Silent no-op replacements
# are how a bootstrap script quietly half-renames a project.
function Set-RequiredText {
    param(
        [Parameter(Mandatory)][string]$Text,
        [Parameter(Mandatory)][string]$Find,
        [Parameter(Mandatory)][AllowEmptyString()][string]$Replace,
        [Parameter(Mandatory)][string]$Context
    )

    if (-not $Text.Contains($Find)) {
        throw "Could not find expected text in ${Context}:`n    $Find`nThe template may have been modified. Aborting before making partial changes."
    }
    return $Text.Replace($Find, $Replace)
}

function Remove-RepoItem {
    param([Parameter(Mandatory)][string]$RelativePath)

    $full = Join-Path $Root $RelativePath
    if (Test-Path -LiteralPath $full) {
        if ($PSCmdlet.ShouldProcess($RelativePath, 'Remove')) {
            Remove-Item -LiteralPath $full -Recurse -Force
        }
        return $true
    }
    return $false
}

# PascalCase / SNAKE_CASE -> kebab-case, keeping acronyms intact:
#   CPPApplicationTemplate -> cpp-application-template
#   MyCoolApp              -> my-cool-app
function ConvertTo-KebabCase {
    param([Parameter(Mandatory)][string]$Value)

    $spaced = [regex]::Replace($Value, '(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])', '-')
    return ($spaced -replace '_+', '-').ToLowerInvariant()
}

function Write-Step {
    param([string]$Message)
    Write-Host '  * ' -ForegroundColor DarkGray -NoNewline
    Write-Host $Message
}

# ---------------------------------------------------------------------------------------
# Resolve derived values and validate preconditions
# ---------------------------------------------------------------------------------------

if (-not $ExecutableName) { $ExecutableName = $ProjectName }

if (-not $PackageName) {
    $PackageName = ConvertTo-KebabCase $ProjectName
    if ($PackageName -notmatch '^[a-z0-9]+(-[a-z0-9]+)*$') {
        throw "Could not derive a valid vcpkg port name from '$ProjectName' (produced '$PackageName'). Pass -PackageName explicitly."
    }
}

if ($ProjectName -eq $TemplateProject) {
    throw "ProjectName matches the template's own name. Pick a name for your project."
}

$cmakeText = Read-RepoFile 'CMakeLists.txt'
if (-not $cmakeText.Contains($TemplateProject)) {
    throw "CMakeLists.txt no longer references '$TemplateProject'. This repository has already been bootstrapped; re-running would corrupt it."
}

$year = (Get-Date).Year

# ---------------------------------------------------------------------------------------
# Plan
# ---------------------------------------------------------------------------------------

Write-Host ''
Write-Host 'Bootstrapping template ->' -ForegroundColor Cyan -NoNewline
Write-Host " $ProjectName" -ForegroundColor White
Write-Host ''
Write-Host ('  {0,-16} {1}' -f 'Project',     $ProjectName)
Write-Host ('  {0,-16} {1}' -f 'Executable',  "$ExecutableName.exe")
Write-Host ('  {0,-16} {1}' -f 'vcpkg port',  $PackageName)
Write-Host ('  {0,-16} {1}' -f 'Version',     $Version)
Write-Host ('  {0,-16} {1}' -f 'Description', $Description)
Write-Host ('  {0,-16} {1}' -f 'Copyright',   "$year $Author")
Write-Host ('  {0,-16} {1}' -f 'Example code', $(if ($KeepExample) { 'kept' } else { 'replaced with stubs' }))
Write-Host ('  {0,-16} {1}' -f 'Git history', $(if ($ResetGit) { 'DELETED and reinitialised' } else { 'left alone' }))
Write-Host ''

if ($ResetGit -and -not $Force -and -not $WhatIfPreference) {
    Write-Host 'WARNING: -ResetGit permanently deletes this repository''s .git directory.' -ForegroundColor Yellow
}

if (-not $Force -and -not $WhatIfPreference) {
    $answer = Read-Host 'Proceed? [y/N]'
    if ($answer -notmatch '^(y|yes)$') {
        Write-Host 'Aborted, nothing was changed.' -ForegroundColor Yellow
        return
    }
    Write-Host ''
}

# ---------------------------------------------------------------------------------------
# CMakeLists.txt
# ---------------------------------------------------------------------------------------

Write-Step 'CMakeLists.txt'

$cmakeText = Set-RequiredText -Text $cmakeText -Context 'CMakeLists.txt' `
    -Find "project($TemplateProject" -Replace "project($ProjectName"

$cmakeText = Set-RequiredText -Text $cmakeText -Context 'CMakeLists.txt' `
    -Find 'VERSION 0.1.0.0' -Replace "VERSION $Version.0"

$cmakeText = Set-RequiredText -Text $cmakeText -Context 'CMakeLists.txt' `
    -Find 'set(WIN32_APP_DESCRIPTION "A template for a C++ application."' `
    -Replace "set(WIN32_APP_DESCRIPTION `"$Description`""

# Only pin EXECUTABLE_NAME when it genuinely differs; otherwise leave it tracking
# ${PROJECT_NAME} so a later project() rename stays consistent.
if ($ExecutableName -ne $ProjectName) {
    $cmakeText = Set-RequiredText -Text $cmakeText -Context 'CMakeLists.txt' `
        -Find 'set(EXECUTABLE_NAME ${PROJECT_NAME} CACHE STRING' `
        -Replace "set(EXECUTABLE_NAME `"$ExecutableName`" CACHE STRING"
}

Write-RepoFile 'CMakeLists.txt' $cmakeText

# ---------------------------------------------------------------------------------------
# vcpkg.json
# ---------------------------------------------------------------------------------------

Write-Step 'vcpkg.json'

$vcpkgText = Read-RepoFile 'vcpkg.json'
$vcpkgText = Set-RequiredText -Text $vcpkgText -Context 'vcpkg.json' `
    -Find "`"name`": `"$TemplatePackage`"" -Replace "`"name`": `"$PackageName`""
$vcpkgText = Set-RequiredText -Text $vcpkgText -Context 'vcpkg.json' `
    -Find '"version": "0.1.0"' -Replace "`"version`": `"$Version`""
$vcpkgText = Set-RequiredText -Text $vcpkgText -Context 'vcpkg.json' `
    -Find '"description": "A C++ application template."' `
    -Replace "`"description`": `"$($Description.Replace('"', '\"'))`""

Write-RepoFile 'vcpkg.json' $vcpkgText

# ---------------------------------------------------------------------------------------
# LICENSE
# ---------------------------------------------------------------------------------------

Write-Step 'LICENSE'

$licenseText = Read-RepoFile 'LICENSE'
$licenseText = [regex]::Replace(
    $licenseText,
    'Copyright \(c\) \d{4}(?:-\d{4})? .+',
    "Copyright (c) $year $Author"
)
Write-RepoFile 'LICENSE' $licenseText

# ---------------------------------------------------------------------------------------
# README.md
# ---------------------------------------------------------------------------------------

Write-Step 'README.md'

$readmeText = Read-RepoFile 'README.md'

# Swap the template's title and subtitle for the project's own.
$readmeText = [regex]::Replace(
    $readmeText,
    '(?s)^# C\+\+ Application Template\r?\n\r?\n.*?\r?\n\r?\n---',
    "# $ProjectName`r`n`r`n$Description`r`n`r`n---"
)

# That section documents this script, which is about to delete itself. Keeping it would
# leave a real project's README describing a workflow it can no longer run.
$readmeText = [regex]::Replace(
    $readmeText,
    '(?sm)^## Bootstrapping a new project.*?^(?=## Notes and known tradeoffs)',
    ''
)

$readmeText = $readmeText.Replace($TemplateProject, $ProjectName)
$readmeText = $readmeText.Replace($TemplatePackage, $PackageName)

Write-RepoFile 'README.md' $readmeText

# ---------------------------------------------------------------------------------------
# Example sources
# ---------------------------------------------------------------------------------------

if (-not $KeepExample) {
    Write-Step 'Replacing example sources with stubs'

    foreach ($file in 'src/app.cpp', 'src/app.hpp', 'tests/test_app.cpp') {
        [void](Remove-RepoItem $file)
    }

    Write-RepoFile 'src/main.cpp' @"
#include <iostream>

int main() {
    std::cout << "Hello from $ProjectName!" << '\n';

    return 0;
}
"@

    # tests/ globs its sources, and an add_executable with no sources is a configure error.
    # The test presets also set noTestsAction: error, so CTest needs at least one case.
    Write-RepoFile 'tests/test_placeholder.cpp' @"
#include <catch2/catch_test_macros.hpp>

// Placeholder so the test target has a source and CTest has a case to run.
// Delete this once $ProjectName has real tests.
TEST_CASE("placeholder", "[placeholder]") {
    REQUIRE(true);
}
"@
}

# ---------------------------------------------------------------------------------------
# Generated output
# ---------------------------------------------------------------------------------------

Write-Step 'Clearing generated output'

foreach ($path in 'build', 'install', 'dist', 'vcpkg_installed', '.clangd') {
    if (Remove-RepoItem $path) {
        Write-Verbose "Removed $path"
    }
}

# ---------------------------------------------------------------------------------------
# Git
# ---------------------------------------------------------------------------------------

if ($ResetGit) {
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        Write-Warning 'git was not found on PATH; skipping -ResetGit.'
    }
    elseif ($PSCmdlet.ShouldProcess('.git', 'Delete history and reinitialise')) {
        Write-Step 'Reinitialising git history'

        Remove-Item -LiteralPath (Join-Path $Root '.git') -Recurse -Force -ErrorAction SilentlyContinue

        Push-Location $Root
        try {
            git init -b main --quiet
            git add -A
            git commit --quiet -m "Initial commit

Bootstrapped from cpp-application-template."
        }
        finally {
            Pop-Location
        }
    }
}

# ---------------------------------------------------------------------------------------
# Self-destruct
# ---------------------------------------------------------------------------------------

if (-not $KeepScript) {
    Write-Step 'Removing bootstrap script'
    # Safe to delete while running: PowerShell has already read the whole file.
    [void](Remove-RepoItem 'BOOTSTRAP_TEMPLATE.ps1')
}

# ---------------------------------------------------------------------------------------
# Done
# ---------------------------------------------------------------------------------------

Write-Host ''
if ($WhatIfPreference) {
    Write-Host 'Dry run complete, nothing was changed.' -ForegroundColor Yellow
    Write-Host ''
    return
}

Write-Host "$ProjectName is ready." -ForegroundColor Green
Write-Host ''
Write-Host 'Next steps:' -ForegroundColor Cyan
Write-Host '  1. Replace assets/AppIcon.ico with your own icon.'
Write-Host '  2. Confirm VCPKG_ROOT is set, then build:'
Write-Host '       cmake --workflow --preset ninja-debug' -ForegroundColor White
if (-not $ResetGit) {
    Write-Host '  3. Reset git history if you cloned this from the template repository.'
}
Write-Host ''
