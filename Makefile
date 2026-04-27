PREFIX    ?= /opt/market-visualizer
BINNAME    = market-visualizer
BINARY     = build/thesis
LINKDIR    = /usr/local/bin
APPDIR     = $(HOME)/.local/share/applications
DESKTOP    = $(APPDIR)/$(BINNAME).desktop
# Absolute path to this repo — embedded in the desktop entry so relative
# data paths (data/MANIPULATION_WINDOWS/...) resolve correctly when launched
# from the app menu.
PROJECT_DIR := $(shell pwd)

.PHONY: all build install uninstall

all: build

build:
	cmake --build build --parallel 4

install: build
	sudo cmake --install build --prefix $(PREFIX)
	sudo ln -sf $(PREFIX)/thesis $(LINKDIR)/$(BINNAME)
	mkdir -p $(APPDIR)
	@{ \
	  echo '[Desktop Entry]'; \
	  echo 'Version=1.0'; \
	  echo 'Type=Application'; \
	  echo 'Name=Market Visualizer'; \
	  echo 'Comment=MULN spoofing detection — thesis research tool'; \
	  echo 'Exec=$(PREFIX)/thesis gui'; \
	  echo 'Path=$(PROJECT_DIR)'; \
	  echo 'Icon=utilities-system-monitor'; \
	  echo 'Categories=Science;Education;'; \
	  echo 'Terminal=false'; \
	  echo 'StartupNotify=true'; \
	} > $(DESKTOP)
	update-desktop-database $(APPDIR) 2>/dev/null || true
	@echo ""
	@echo "Installed:    $(PREFIX)/thesis"
	@echo "Symlink:      $(LINKDIR)/$(BINNAME)"
	@echo "Desktop:      $(DESKTOP)"
	@echo "Working dir:  $(PROJECT_DIR)"

uninstall:
	sudo rm -f $(PREFIX)/$(BINNAME)
	sudo rm -f $(LINKDIR)/$(BINNAME)
	rm -f $(DESKTOP)
	update-desktop-database $(APPDIR) 2>/dev/null || true
	@echo "Uninstalled."
