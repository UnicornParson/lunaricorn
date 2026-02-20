#!/bin/bash
set -e
VENV_DIR="venv"

if ! command -v python3 &> /dev/null; then
    echo "Error: Python 3 is not installed."
    exit 1
fi

if [ ! -d "$VENV_DIR" ]; then
    echo "Creating virtual environment in $VENV_DIR..."
    python3 -m venv "$VENV_DIR"
    if [ $? -ne 0 ]; then
        echo "Error creating virtual environment."
        exit 1
    fi
else
    echo "Virtual environment $VENV_DIR already exists."
fi

if [ -f "requirements.txt" ]; then
    echo "Installing dependencies from requirements.txt..."
    "$VENV_DIR/bin/pip" install -r requirements.txt
    if [ $? -ne 0 ]; then
        echo "Error installing dependencies."
        exit 1
    fi
else
    echo "requirements.txt not found, skipping dependency installation."
fi


echo "Running $SCRIPT ..."
"$VENV_DIR/bin/python" jita_loader.py