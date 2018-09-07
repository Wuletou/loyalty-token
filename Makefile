NAME=loyalty-token
ACCOUNT=$(word 3, $(shell cat config.h | grep EXCHANGE))
VERSION=$(shell git tag --points-at HEAD)
VERSIONHASH=$(shell git rev-parse HEAD)

all: compile publish

compile: $(NAME)/$(NAME).wasm

$(NAME)/$(NAME).wasm: $(NAME).cpp
	eosiocpp -o $(NAME)/$(NAME).wast $(NAME).cpp

publish:
	$(eval VERSION_L := $(shell printf "%02x" `printf "%s" "$(VERSION)" | wc -c`))
	$(eval VERSION_B := $(shell printf "%s" "$(VERSION)" | xxd -p | tr -d '\n'))
	$(eval VERSIONHASH_L := $(shell printf "%02x" `printf "%s" "$(VERSIONHASH)" | wc -c`))
	$(eval VERSIONHASH_B := $(shell printf "%s" "$(VERSIONHASH)" | xxd -p | tr -d '\n'))
	$(eval SETVER := $(VERSION_L)$(VERSION_B)$(VERSIONHASH_L)$(VERSIONHASH_B))
	$(eval ABI := $(shell cleos -u http://127.0.0.1:8887 set abi $(ACCOUNT) $(NAME)/$(NAME).abi -jd 2> /dev/null | grep \"data\" | cut -c 16- | rev | cut -c 2- | rev))
	$(eval BYTECODE := $(shell cat $(NAME)/$(NAME).wasm | xxd -p | tr -d '\n'))
	$(eval TX := '{\"account\": \"eosio\", \"name\": \"setcode\", \"authorization\": [{\"actor\": \"$(ACCOUNT)\", \"permission\": \"active\"}], \"data\": {\"account\": \"$(ACCOUNT)\", \"vmtype\": 0, \"vmversion\": 0, \"code\": \"$(BYTECODE)\"}},')
	$(eval TX += '{\"account\": \"eosio\", \"name\": \"setabi\", \"authorization\": [{\"actor\": \"$(ACCOUNT)\", \"permission\": \"active\"}], \"data\": \"$(ABI)\"},')
	$(eval TX += '{\"account\": \"$(ACCOUNT)\", \"name\": \"setver\", \"authorization\": [{\"actor\": \"$(ACCOUNT)\", \"permission\": \"active\"}], \"data\": \"$(SETVER)\"}')
	@echo '{"actions": [$(TX)]}' > .tx
	cleos -u http://127.0.0.1:8887 push transaction .tx > /dev/null
	@rm .tx
