bin_SCRIPTS = osdlyrics-create-lyricsource \
	osdlyrics-layer-osd

osdlyricstoolsdir = $(pkglibdir)/tools
osdlyricstools_PYTHON = \
	create-lyricsource.py \
	$(NULL)

osdlyrics-create-lyricsource: osdlyrics-create-lyricsource.in
	@sed -e "s|\@pkglibdir\@|$(pkglibdir)|" -e "s|\@PYTHON\@|$(PYTHON)|" $< > $@
	@chmod +x $@

osdlyrics-layer-osd: osdlyrics-layer-osd.in
	@sed -e "s|\@PYTHON\@|$(PYTHON)|" $< > $@
	@chmod +x $@

CLEANFILES = \
	osdlyrics-create-lyricsource \
	osdlyrics-layer-osd \
	$(NULL)
