
all:
	@$(MAKE) -C test

clean:
	@$(MAKE) clean -C test

install:
	@$(MAKE) install -C src