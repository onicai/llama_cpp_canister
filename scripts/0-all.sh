#!/bin/bash

scripts/1-build.sh
scripts/2-deploy-reinstall.sh
scripts/3-upload-model.sh
scripts/4-load-model.sh
scripts/5-set-max-tokens.sh