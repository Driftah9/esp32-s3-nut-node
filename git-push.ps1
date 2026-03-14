# git-push.ps1 - Unified GitHub Push Script
# Reads all push details from docs\github_push.md
# Git root is the project root (where this script lives)
# Works for all project types (ESP32, Software, any)
# Detects first push vs subsequent commits
# Checks for changes before doing anything
# Requires Y/N confirmation before pushing

# === PATHS ===

$ProjectRoot = $PSScriptRoot
$GithubPushPath = Join-Path $ProjectRoot "docs\github_push.md"

# === VALIDATE github_push.md EXISTS ===

if (-not (Test-Path $GithubPushPath)) {
    Write-Host ""
    Write-Host "ERROR: docs\github_push.md not found." -ForegroundColor Red
    Write-Host "Claude must populate this file before pushing." -ForegroundColor Red
    exit 1
}

# === READ docs\github_push.md ===

$pushContent = Get-Content $GithubPushPath -Raw

# Repo
$repoMatch = [regex]::Match($pushContent, '(?m)^## Repo\s*\n(.+?)$')
if (-not $repoMatch.Success -or $repoMatch.Groups[1].Value.Trim() -eq "") {
    Write-Host ""; Write-Host "ERROR: Could not read Repo from docs\github_push.md." -ForegroundColor Red; exit 1
}
$RepoRaw = $repoMatch.Groups[1].Value.Trim()
if ($RepoRaw -match "github\.com/[^/]+/(.+)$") { $RepoName = $Matches[1].TrimEnd('/') } else { $RepoName = $RepoRaw }

# Project Name
$nameMatch = [regex]::Match($pushContent, '(?m)^## Project\s*\n(.+?)$')
if (-not $nameMatch.Success -or $nameMatch.Groups[1].Value.Trim() -eq "") {
    Write-Host ""; Write-Host "ERROR: Could not read Project from docs\github_push.md." -ForegroundColor Red; exit 1
}
$ProjectName = $nameMatch.Groups[1].Value.Trim()

# Version
$versionMatch = [regex]::Match($pushContent, '(?m)^## Version\s*\n(.+?)$')
$Version = ""; $Tag = ""; $TagMessage = ""; $HasVersion = $false
if ($versionMatch.Success -and $versionMatch.Groups[1].Value.Trim() -ne "" -and $versionMatch.Groups[1].Value.Trim() -ne "N/A") {
    $Version = $versionMatch.Groups[1].Value.Trim()
    $Tag = $Version; $TagMessage = "$Version - $ProjectName"; $HasVersion = $true
}

# Visibility
$visibilityMatch = [regex]::Match($pushContent, '(?m)^## Visibility\s*\n(.+?)$')
$RepoVisibility = "public"
if ($visibilityMatch.Success -and $visibilityMatch.Groups[1].Value.Trim() -ne "") { $RepoVisibility = $visibilityMatch.Groups[1].Value.Trim() }

# Branch
$branchMatch = [regex]::Match($pushContent, '(?m)^## Branch\s*\n(.+?)$')
$Branch = "main"
if ($branchMatch.Success -and $branchMatch.Groups[1].Value.Trim() -ne "") { $Branch = $branchMatch.Groups[1].Value.Trim() }

# Commit Message
$commitMatch = [regex]::Match($pushContent, '(?ms)^## Commit Message\s*\n(.+?)(?=\n## |\z)')
if (-not $commitMatch.Success -or $commitMatch.Groups[1].Value.Trim() -eq "") {
    Write-Host ""; Write-Host "ERROR: Could not read Commit Message from docs\github_push.md." -ForegroundColor Red; exit 1
}
$CommitBody = $commitMatch.Groups[1].Value.Trim()
if ($CommitBody -match "^\[" -or $CommitBody -match "^placeholder" -or $CommitBody -match "^TBD" -or $CommitBody -match "^- Change one") {
    Write-Host ""; Write-Host "ERROR: Commit Message is still a placeholder." -ForegroundColor Red; exit 1
}
if ($HasVersion) { $CommitMessage = "$Version - $ProjectName`n`n$CommitBody" } else { $CommitMessage = "$ProjectName update`n`n$CommitBody" }

# === GIT ROOT IS PROJECT ROOT ===
# git add . from project root handles everything: src\current, .github, docs, scripts
Set-Location $ProjectRoot

# === DETECT FIRST PUSH ===
$IsFirstPush = $false
if (-not (Test-Path ".git")) {
    $IsFirstPush = $true
} else {
    $remote = git remote get-url origin 2>$null
    if (-not $remote) { $IsFirstPush = $true }
}

# === CHECK FOR CHANGES ===
if (-not $IsFirstPush) {
    $gitStatus = git status --porcelain 2>&1
    if (-not $gitStatus) {
        Write-Host ""
        Write-Host "=======================================" -ForegroundColor Yellow
        Write-Host " No Changes Detected" -ForegroundColor Yellow
        Write-Host "=======================================" -ForegroundColor Yellow
        Write-Host "Nothing to commit - working tree is clean." -ForegroundColor Gray
        if ($HasVersion) { $existingTag = git tag -l $Tag; if ($existingTag) { Write-Host "Tag $Tag already exists on GitHub." -ForegroundColor Gray } }
        Write-Host ""; Write-Host "No action taken." -ForegroundColor Gray; exit 0
    }
}

# === SHOW SUMMARY AND CONFIRM ===
$IsEsp32 = Test-Path (Join-Path $ProjectRoot "src\current")
Write-Host ""
Write-Host "=======================================" -ForegroundColor Cyan
Write-Host " GitHub Push - Confirm" -ForegroundColor Cyan
Write-Host "=======================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Project   : $ProjectName" -ForegroundColor Gray
Write-Host "Repo      : $RepoName ($RepoVisibility)" -ForegroundColor Gray
Write-Host "Branch    : $Branch" -ForegroundColor Gray
if ($HasVersion) { Write-Host "Version   : $Version" -ForegroundColor Gray; Write-Host "Tag       : $Tag" -ForegroundColor Gray }
else { Write-Host "Tag       : None" -ForegroundColor Gray }
if ($IsEsp32) { Write-Host "Type      : ESP32 (git root: project root)" -ForegroundColor Gray }
else { Write-Host "Type      : Software (project root)" -ForegroundColor Gray }
if ($IsFirstPush) { Write-Host "Mode      : FIRST PUSH (will create GitHub repo)" -ForegroundColor Yellow }
else { Write-Host "Mode      : Commit + Push" -ForegroundColor Gray }
Write-Host ""
Write-Host "Commit message:" -ForegroundColor Gray
Write-Host "----------------------------------------" -ForegroundColor DarkGray
Write-Host $CommitMessage -ForegroundColor White
Write-Host "----------------------------------------" -ForegroundColor DarkGray
Write-Host ""
Write-Host "Changed files:" -ForegroundColor Gray
git status --short
Write-Host ""

$confirm = Read-Host "Push to GitHub? (Y/N)"
if ($confirm -notmatch "^[Yy]$") { Write-Host ""; Write-Host "Cancelled." -ForegroundColor Yellow; exit 0 }

# === FIRST PUSH ===
if ($IsFirstPush) {
    Write-Host ""; Write-Host "--- Initialising git repo ---" -ForegroundColor Yellow
    git init
    git checkout -b $Branch

    Write-Host ""; Write-Host "--- Staging all files ---" -ForegroundColor Yellow
    git add .
    if ($LASTEXITCODE -ne 0) { Write-Host "ERROR: git add failed" -ForegroundColor Red; exit 1 }

    Write-Host ""; Write-Host "--- Creating GitHub repo: $RepoName ($RepoVisibility) ---" -ForegroundColor Yellow
    gh repo create $RepoName --$RepoVisibility --source=. --remote=origin
    if ($LASTEXITCODE -ne 0) { Write-Host "ERROR: GitHub repo creation failed." -ForegroundColor Red; exit 1 }

    Write-Host ""; Write-Host "--- Initial commit ---" -ForegroundColor Yellow
    git commit -m $CommitMessage
    if ($LASTEXITCODE -ne 0) { Write-Host "ERROR: git commit failed" -ForegroundColor Red; exit 1 }

    Write-Host ""; Write-Host "--- Pushing to origin/$Branch ---" -ForegroundColor Yellow
    git push -u origin $Branch
    if ($LASTEXITCODE -ne 0) { Write-Host "ERROR: git push failed" -ForegroundColor Red; exit 1 }

} else {
# === SUBSEQUENT PUSH ===
    Write-Host ""; Write-Host "--- Staging changes ---" -ForegroundColor Yellow
    git add .
    if ($LASTEXITCODE -ne 0) { Write-Host "ERROR: git add failed" -ForegroundColor Red; exit 1 }

    Write-Host ""; Write-Host "--- Committing ---" -ForegroundColor Yellow
    git commit -m $CommitMessage
    if ($LASTEXITCODE -ne 0) { Write-Host "ERROR: git commit failed" -ForegroundColor Red; exit 1 }

    Write-Host ""; Write-Host "--- Pushing to origin/$Branch ---" -ForegroundColor Yellow
    git push origin $Branch
    if ($LASTEXITCODE -ne 0) { Write-Host "ERROR: git push failed" -ForegroundColor Red; exit 1 }
}

# === TAGGING ===
if ($HasVersion) {
    Write-Host ""; Write-Host "--- Tagging $Tag ---" -ForegroundColor Yellow
    $existingTag = git tag -l $Tag
    if ($existingTag) {
        Write-Host "Tag $Tag already exists - skipping." -ForegroundColor Yellow
    } else {
        git tag -a $Tag -m $TagMessage
        if ($LASTEXITCODE -ne 0) { Write-Host "ERROR: git tag failed" -ForegroundColor Red; exit 1 }
        git push origin $Tag
        if ($LASTEXITCODE -ne 0) { Write-Host "ERROR: git push tag failed" -ForegroundColor Red; exit 1 }
        Write-Host "Tagged and pushed: $Tag" -ForegroundColor Green
    }
}

# === DONE ===
Write-Host ""
Write-Host "=======================================" -ForegroundColor Green
Write-Host " Done!" -ForegroundColor Green
if ($IsFirstPush) { Write-Host " Repo created and initial push complete." -ForegroundColor Green }
elseif ($HasVersion) { Write-Host " Pushed: $Branch + tag $Tag" -ForegroundColor Green }
else { Write-Host " Pushed: $Branch" -ForegroundColor Green }
Write-Host "=======================================" -ForegroundColor Green
Write-Host ""
