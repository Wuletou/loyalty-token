#pragma once

#include <eosiolib/asset.hpp>
#include <eosiolib/eosio.hpp>
#include <eosiolib/singleton.hpp>

#include <string>

class loyaltytoken : public eosio::contract {
private:
	struct account {
		eosio::asset balance;
		int64_t blocked;
		uint64_t primary_key() const { return balance.symbol.name(); }
	};

	struct store_info {
		std::string name;
		std::string url;
		std::string logo_url;
	};

	struct currency_stats {
		eosio::asset supply;
		eosio::asset max_supply;
		account_name issuer;
		store_info info;
		uint64_t primary_key() const { return supply.symbol.name(); }
	};

	struct symbols_t {
		eosio::symbol_name symbol;
		uint64_t primary_key() const { return symbol; }
	};

	struct claim_t {
		uint64_t pk;
		account_name to;
		eosio::asset quantity;
		uint64_t primary_key() const { return pk; }
		uint128_t get_to_quantity() const { return ((uint128_t)to << 64) + quantity.symbol; }
	};

	struct version_t {
		std::string ver;
		std::string hash;
	};

	struct state_t {
		version_t version;
	};

	typedef eosio::multi_index<N(accounts), account> accounts;
	typedef eosio::multi_index<N(stat), currency_stats> stats;
	typedef eosio::multi_index<N(symbols), symbols_t> symbols;
	typedef eosio::multi_index<N(claim), claim_t, eosio::indexed_by<N(toquantity), eosio::const_mem_fun<claim_t, uint128_t, &claim_t::get_to_quantity>>> claims;

	account_name exchange;

	eosio::singleton<N(state), state_t> state_singleton;

	bool clean;
	state_t state;

	state_t default_parameters() {
		return state_t{
			.version = version_t{
				.ver = "",
				.hash = ""
			}
		};
	}

	void sub_balance(account_name owner, eosio::asset value, account_name ram_payer);
	void add_balance(account_name owner, eosio::asset value, account_name ram_payer);

public:
	loyaltytoken(account_name self);
	~loyaltytoken();

	void create(account_name issuer, eosio::asset maximum_supply, store_info info);
	void issue(account_name to, eosio::asset quantity, std::string memo);
	void allowclaim(account_name from, account_name to, eosio::asset quantity);
	void claim(account_name from, account_name to, eosio::asset quantity);
	void burn(account_name owner, eosio::asset value);
	void cleanstate();

	inline eosio::asset get_supply(eosio::symbol_name sym) const;
	inline eosio::asset get_balance(account_name owner, eosio::symbol_name sym) const;
};

eosio::asset loyaltytoken::get_supply(eosio::symbol_name sym) const {
	stats statstable(this->_self, sym);
	const auto& st = statstable.get(sym);
	return st.supply;
}

eosio::asset loyaltytoken::get_balance(account_name owner, eosio::symbol_name sym) const {
	accounts accountstable(this->_self, owner);
	const auto& ac = accountstable.get(sym);
	eosio::asset balance = ac.balance;
	balance.amount -= ac.blocked;
	return balance;
}
