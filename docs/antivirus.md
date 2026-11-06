# Antivirus warnings on Windows

Some antivirus tools flag the Windows download. It is a false positive. This page says why and how to check the file yourself.

## Why it happens

The Windows build is not code-signed. Code signing needs a paid certificate tied to a legal identity, and this project runs under a handle, so the binaries ship unsigned. The installer is also built with Inno Setup, which many machine-learning scanners treat as suspicious, because installers unpack and write files the way some malware droppers do.

An unsigned installer plus a freshly compiled binary on each release is enough for a few heuristic engines to guess "malicious", even when there is nothing malicious in the file. The guess shifts from build to build: one release scores clean, the next trips two or three engines.

## How to tell it is a false positive

- Most engines pass it, including every major signature-based product. The [table below](#virustotal-results) has the score for each release.
- The engines that flag it give generic machine-learning verdicts (names like "MALICIOUS" or "ML.Attribute.HighConfidence") and name no specific malware family.
- The code is open source, so you can read it and build the same binaries yourself.

## VirusTotal results

The Windows setup of each release, as VirusTotal scored it when I scanned it.

| Release | File | SHA-256 | VirusTotal | Flagged |
|---|---|---|---|---|
| 0.3.1 | `Takedawn-0.3.1-windows-x64-setup.exe` | `5d571a9896be77defb96bc5a1e8552cf70eee83c91e907c6d852aaefcfa274cd` | [report](https://www.virustotal.com/gui/file/5d571a9896be77defb96bc5a1e8552cf70eee83c91e907c6d852aaefcfa274cd) | 1/68 (Deep Instinct) |
| 0.3.0 | `Takedawn-0.3.0-windows-x64-setup.exe` | `c549a139fa3744a5029c7d63170acc363549395b99fbcd12e65d436239670dbc` | [report](https://www.virustotal.com/gui/file/c549a139fa3744a5029c7d63170acc363549395b99fbcd12e65d436239670dbc) | 1/68 (Deep Instinct) |
| 0.2.1 | `Takedawn-0.2.1-windows-x64-setup.exe` | `85ab4e81ece174f29656f9e39bf68bc39cd7e1f0018870d89a49bc596d06da7e` | [report](https://www.virustotal.com/gui/file/85ab4e81ece174f29656f9e39bf68bc39cd7e1f0018870d89a49bc596d06da7e) | 3/59 (Deep Instinct, SecureAge, Kingsoft) |

The SHA-256 is the line for that file in the release's `SHA256SUMS`. If the
hash of your download matches, the report is about your file.

## What you can do

- Check the file's SHA-256 against the hash on the release page before you run it.
- If your scanner blocks it, allowlist the file or build from source.
- Report the false positive to your antivirus vendor. Vendors remove a detection when they get such reports.

## What I do

After each release I report the false positives to the flagging vendors. Signing the Windows binaries would prevent most of these warnings, but that needs a certificate and a legal identity, so it is a separate decision.
