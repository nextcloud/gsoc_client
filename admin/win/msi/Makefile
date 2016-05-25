# vim: ts=4:sw=4

export WIXPATH := $(HOME)/Downloads/wix310
export PATH := $(WIXPATH):$(PATH)


# Customize here ############

MSI  = owncloud.msi
PRODUCT = ownCloud
MANUFAC = ownCloud
EXEC    = owncloud.exe

# Leave this alone ##########

OBJ  = files.wixobj product.wixobj
EXE  = $(MSI:.msi=.exe)
PDB  = $(MSI:.msi=.wixpdb)
WXL  = $(wildcard l10n/*.wxl)

HEAT_OPTS  = -var var.SourceDir -nologo -cg Files -gg -dr FilesDir -template fragment -sw5150 -suid
LIGHT_OPTS = -nologo -sval -cultures:en-us -loc l10n/en-us.wxl 
#LIGHT_OPTS = -nologo -sval -ext WixUIExtension -cultures:en-us


# Fixme: Spaces not working, switch to .in?
CANDLE_DEFINES = -dSourceDir=ownCloud -dProductName=$(PRODUCT) -dManufacturer=$(MANUFAC) -dInstallDir=$(PRODUCT) -dInfoURL=http://owncloud.com -dMainExecutable=$(EXEC)

$(MSI): $(OBJ) $(WXL)
	@echo Creating MSI $@ from $(OBJ)...
	light $(LIGHT_OPTS)  $(OBJ) -out $@
	@chmod 664  $@

$(EXE): $(MSI)
	burn $< $@

%.wixobj: %.wxs
	@echo Creating $@ from $<...
	candle -nologo $(CANDLE_DEFINES) $<

files.wxs:
	@echo Harvesting files to $@...
	heat dir ownCloud $(HEAT_OPTS) -o files.wxs

.PHONY: clean

clean:
	rm -f $(MSI) $(OBJ) $(PDB) $(EXE) files.wxs
