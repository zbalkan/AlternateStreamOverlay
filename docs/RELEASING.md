# Release procedure

Releases are created by the [Manual Release](../.github/workflows/release.yml) workflow. The workflow runs on `main` only and creates a **draft** GitHub release; publishing it requires a separate manual action.

## Before triggering

1. Merge and review the intended release changes on `main`; confirm the latest CI, native tests, and shell-registration checks are successful.
2. Update `src/version.h` to the intended `MAJOR.MINOR.PATCH` with `ASO_VERSION_BUILD 0`.
3. Ensure the latest versioned section of `CHANGELOG.md` matches, and review its release date and notes. Preserve the original historical changelog entries.
4. Confirm that the target tag (for example `v2.0.0`) and GitHub release do not exist.

## Stage a draft

In GitHub Actions, select **Manual Release**, choose `main`, enter the unprefixed version (for example `2.0.0`), and run the workflow.

The workflow validates the source version, checks GitHub for an existing tag/release, builds x64 Release, verifies the DLL and EXE VERSIONINFO, runs native MSTest and ADS smoke tests, and generates a ZIP and SHA-256 checksum. It then creates a draft release and uploads a copy of the release assets as a workflow artifact.

A workflow failure is **not** a successful release. Inspect logs before retrying. If a draft release or tag already exists after a partial failure, review and clean it up explicitly rather than bypassing the guard.

## Verify before publication

1. Download the draft release's ZIP and companion `.sha256` file.
2. On Windows, compare the archive's SHA-256 value with the value in the checksum file:

   `(Get-FileHash -Algorithm SHA256 .\AlternateStreamOverlay-v2.0.0-win-x64.zip).Hash.ToLowerInvariant()`

3. Extract the archive. Check that it contains `AlternateStreamOverlay.dll`, `altstrm.exe`, `README.md`, and `CHANGELOG.md`.
4. Inspect the DLL and EXE **File version**, **Product version**, **Internal name**, and **Original filename** in file properties; both numerical versions must be `MAJOR.MINOR.PATCH.0`.
5. Test registration, property-page behaviour, stream previews, and unregistration on a disposable Windows 10/11 x64 NTFS test machine. Automated runner tests are not a substitute for observing Explorer integration.
6. Confirm that the draft points to the intended `main` commit, its release notes match the changelog, and the assets correspond to the validated build.

Publish the verified draft manually. Do not rebuild or substitute assets after verification without repeating these checks.

## Recovery

If validation fails before the draft is created, fix the cause and rerun the workflow. If a draft or tag was created, inspect its assets and references before deleting or replacing anything. Never overwrite an existing published release as part of an automated retry.
