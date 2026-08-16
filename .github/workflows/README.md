# `.github/workflows/` — GitHub Actions automation

The workflows automate catalog validation, generation/persistence and GitHub Pages publication. The generated Homebrew catalogs are:

```text
catalog.json
authors.json
categories.json
```

## Catalog workflow

`validate.yml` currently coordinates the catalog pipeline around steps such as:

```text
checkout
   ↓
determine incremental/rebuild mode
   ↓
external smoke tests
   ↓
generate / aggregate catalog
   ↓
normalize persisted source data
   ↓
validate catalog and registry
   ↓
verify generated outputs
   ↓
publish changes when the workflow is configured to do so
```

The exact command order is defined by the workflow file itself. This README is an operational guide, not a replacement for the YAML configuration.

## GitHub Pages

`pages.yml` publishes the static `web/` application through GitHub Pages. The website consumes generated catalog data and does not require a custom backend.

## Automatic execution

The active workflow configuration determines which events run generation, validation and publication. Always inspect the current YAML before changing trigger behavior.

## Concurrency

The catalog publishing workflow uses a concurrency group so two publisher executions do not modify `main` simultaneously. This is important because concurrent runs can otherwise create Git conflicts when both generate the same author/application files.

The intended behavior is to wait for the active publisher rather than silently discard the newer data run.

## Incremental mode and rebuilds

The workflow can distinguish incremental processing from a full external rebuild according to the current workflow markers and source state.

A rebuild still follows the canonical architecture: it reconstructs canonical data and then regenerates the public catalogs. It does not mean manually editing generated JSON files.

## Publication safety

`catalog.json`, `authors.json` and `categories.json` must pass validation before publication.

If validation fails, the workflow should stop before publishing an incomplete catalog.

## Troubleshooting

When a workflow fails, inspect the **first failed step** and its logs rather than relying only on the final `Error` summary. Classify the failure as one of:

- external acquisition;
- normalization/aggregation;
- validation;
- persistence/commit;
- concurrency;
- GitHub Pages deployment.

Fix the layer that actually failed instead of patching generated catalog output.
