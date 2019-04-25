#!/bin/bash -ex

# Converts "citra-emu/citra-nightly" to "citra-nightly"
REPO_NAME=$(echo $BUILD_REPOSITORY_NAME | cut -d'/' -f 2)
CITRA_SRC_DIR="$BUILD_SOURCESDIRECTORY/"
BUILD_DIR="$CITRA_SRC_DIR/build"
REPO_DIR="$CITRA_SRC_DIR/repo"
STATE_DIR="$CITRA_SRC_DIR/.flatpak-builder"
SSH_DIR="/upload"
SSH_KEY="${AZP_TMP_DIR}/flatpak_ssh.key"
GPG_KEY="${AZP_TMP_DIR}/flatpak_gpg.key"

# Configure SSH keys
mkdir -p "$HOME/.ssh"
chmod 0700 "$HOME/.ssh"
eval "$(ssh-agent -s)"
chmod 0600 "$SSH_KEY"
ssh-add "$SSH_KEY"
echo "[$FLATPAK_SSH_HOSTNAME]:$FLATPAK_SSH_PORT,[$(dig +short $FLATPAK_SSH_HOSTNAME)]:$FLATPAK_SSH_PORT $FLATPAK_SSH_HOST_FINGERPRINT" > "$HOME/.ssh/known_hosts"

# Configure GPG keys
gpg2 --import "$GPG_KEY"

# Mount our flatpak repository
mkdir -p "$REPO_DIR"
sshfs "$FLATPAK_SSH_USER@$FLATPAK_SSH_HOSTNAME:$SSH_DIR" "$REPO_DIR" -o idmap=user -C -p "$FLATPAK_SSH_PORT" -o IdentityFile="$SSH_KEY"

stat "$REPO_DIR"
sudo chmod a+rw "$REPO_DIR"

# setup ccache location
# mkdir -p "$STATE_DIR"
# ln -sv /root/.ccache "$STATE_DIR/ccache"

# Build the citra flatpak
sudo flatpak-builder -v --jobs=4 --ccache --force-clean --state-dir="$STATE_DIR" --gpg-sign="$FLATPAK_GPG_KEY_ID" --repo="$REPO_DIR" "$BUILD_DIR" "/tmp/org.citra.$REPO_NAME.json"
flatpak build-update-repo "$REPO_DIR" -v --generate-static-deltas --gpg-sign="$FLATPAK_GPG_KEY_ID"
