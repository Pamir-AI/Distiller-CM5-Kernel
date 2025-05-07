# Pamir AI Kernel CI Workflows

This directory contains GitHub Actions workflows for the Pamir AI kernel project.

## Kernel Release CI

The `kernel-release.yml` workflow automates the build and release process for the Pamir AI kernel.

### Usage

#### Manual Execution

1. Navigate to the "Actions" tab in the GitHub repository
2. Select the "Kernel Release CI" workflow
3. Click "Run workflow"
4. Configure the workflow options:
   - **Build mode**: Select the kernel build mode (default: `lto`)
   - **Run make clean**: Whether to clean before building (default: `true`)
   - **Custom tag**: Optionally specify a custom release tag
   - **Version suffix**: Add a custom suffix to the kernel version
   - **Create as draft**: Create a draft release (default: `false`)
   - **Mark as prerelease**: Mark as prerelease (default: `false`)
   - **Changelog text**: Provide release notes/changelog

5. Click "Run workflow" to start the build and release process

#### Scheduled Execution

The workflow also runs automatically on a weekly schedule (Monday at 2:00 AM UTC) with the following default settings:
- Build mode: `lto`
- Clean build: `true`
- Version suffix: `weekly`
- Prerelease: `true`
- Changelog: Automatically generated with the current date

### Workflow Steps

1. Set up the build environment and install dependencies
2. Fetch the latest release tag (to maintain versioning consistency)
3. Build the kernel using the `kernel-build` script
4. Create a GitHub release using the `release-to-github` script

### Required Permissions

The workflow requires `contents: write` permission to create releases. 