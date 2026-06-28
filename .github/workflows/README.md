# CI

GitHub Actions is **enabled** (`build.yml`). On macOS it builds AU + VST3, runs the
unit tests, and validates the plugin with `pluginval` + `auval`, uploading the
built artefacts.

This repo is **public**, so standard-runner minutes are free — leave CI on.

## If you ever make the repo private
macOS runners bill at 10× and would eat the included quota. To pause CI without
deleting it, rename the workflow so GitHub ignores it:
```bash
git mv .github/workflows/build.yml .github/workflows/build.yml.disabled
git commit -m "ci: pause Actions" && git push
```
