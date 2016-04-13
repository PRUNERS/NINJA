include ../Makefile.config

SCRS   = mor.py
SCR_DIR=../scripts/
INSTALL_BIN_DIR=/g/g90/sato5/opt/bin/


install:
	for bin in $(SCRS);     do ( install $$bin  $(INSTALL_BIN_DIR)/$$bin ); done


uninstall: 
	for bin in $(SCRS);     do ( rm $(INSTALL_BIN_DIR)/$$bin ); done

.PHONY: clean
clean:

.PHONY: clean_core
clean_core:


