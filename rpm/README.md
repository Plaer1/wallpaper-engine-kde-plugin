# Building an RPM

These instructions are inherited from upstream and have not been revalidated on
Fedora for this fork. Use the checkout containing the changes you want to package.
On Bazzite/Silverblue, build inside a Fedora toolbox or distrobox matching the host’s
release, then install the resulting RPM from the host.

Enable RPM Fusion for your Fedora release and install the codec/development packages
required by your system. From the repository root:

```sh
sudo dnf builddep ./rpm/wek.spec
git submodule update --init --recursive
rpmbuild --define="commit $(git rev-parse HEAD)" \
    --define="reporoot $(pwd)" \
    --define="glslang_ver 11.8.0" \
    --undefine=_disable_source_fetch \
    -ba ./rpm/wek.spec
```

Install the resulting package:

```sh
# Fedora
sudo dnf install ~/rpmbuild/RPMS/x86_64/wallpaper-engine-kde-plugin-qt6-*.rpm

# rpm-ostree systems: run on the host, then reboot
rpm-ostree install ~/rpmbuild/RPMS/x86_64/wallpaper-engine-kde-plugin-qt6-*.rpm
```

On conventional Fedora, reload Plasma after saving open desktop-settings edits:

```sh
systemctl --user restart plasma-plasmashell.service
```

See the [main README](../README.md) for activation, controls and limitations.
