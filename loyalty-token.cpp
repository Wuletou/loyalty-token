#include "loyalty-token.hpp"
#include "config.h"
#include "str_expand.h"

loyaltytoken::loyaltytoken(account_name self) :
	eosio::contract(self),
	exchange(eosio::string_to_name(STR(EXCHANGE)))
{}

void loyaltytoken::create(account_name issuer, eosio::asset maximum_supply, store_info info) {
	require_auth(this->_self);

	auto sym = maximum_supply.symbol;
	eosio_assert(sym.is_valid(), "invalid symbol name");
	eosio_assert(maximum_supply.is_valid(), "invalid supply");
	eosio_assert(maximum_supply.amount > 0, "max-supply must be positive");

	stats statstable(this->_self, sym.name());
	auto existing = statstable.find(sym.name());
	eosio_assert(existing == statstable.end(), "token with symbol already exists");

	statstable.emplace(this->_self, [&](auto& s) {
		s.supply.symbol = maximum_supply.symbol;
		s.max_supply	= maximum_supply;
		s.issuer = issuer;
		s.info = info;
	});

	symbols symbols_table(this->_self, this->_self);
	symbols_table.emplace(this->_self, [&](auto& s) {
		s.symbol = maximum_supply.symbol;
	});
}

void loyaltytoken::issue(account_name to, eosio::asset quantity, std::string memo) {
	auto sym = quantity.symbol;
	eosio_assert(sym.is_valid(), "invalid symbol name");
	eosio_assert(memo.size() <= 256, "memo has more than 256 bytes");

	auto sym_name = sym.name();
	stats statstable(this->_self, sym_name);
	auto existing = statstable.find(sym_name);
	eosio_assert(existing != statstable.end(), "token with symbol does not exist, create token before issue");
	const auto& st = *existing;

	require_auth(st.issuer);
	eosio_assert(quantity.is_valid(), "invalid quantity");
	eosio_assert(quantity.amount > 0, "must issue positive quantity");

	eosio_assert(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
	eosio_assert(quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

	statstable.modify(st, 0, [&](auto& s) {
		s.supply += quantity;
	});

	add_balance(to, quantity, st.issuer);
}

void loyaltytoken::transfer(account_name from, account_name to, eosio::asset quantity, std::string memo) {
	require_auth(from);
	require_auth(this->exchange);

	eosio_assert(from != to, "cannot transfer to self");
	eosio_assert(is_account(to), "to account does not exist");

	auto sym = quantity.symbol.name();
	stats statstable(this->_self, sym);
	const auto& st = statstable.get(sym);

	require_recipient(from);
	require_recipient(to);

	eosio_assert(quantity.is_valid(), "invalid quantity");
	eosio_assert(quantity.amount > 0, "must transfer positive quantity");
	eosio_assert(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
	eosio_assert(memo.size() <= 256, "memo has more than 256 bytes");

	sub_balance(from, quantity, from);
	add_balance(to, quantity, from);
}

void loyaltytoken::allowclaim(account_name from, eosio::asset quantity) {
	require_auth(from);
	require_auth(this->exchange);

	accounts from_acnts(this->_self, from);
	const auto& account = from_acnts.find(quantity.symbol.name());
	eosio_assert(account != from_acnts.end(), "symbol not found (allowclaim)");
	eosio_assert(account->balance.amount - account->blocked >= quantity.amount, "overdrawn allowclaim");
	from_acnts.modify(account, from, [quantity](auto& a) {
		a.blocked += quantity.amount;
	});
}

void loyaltytoken::claim(account_name from, eosio::asset quantity) {
	require_auth(this->exchange);

	eosio_assert(quantity.amount > 0, "claim must be positive");

	accounts from_acnts(this->_self, from);
	const auto& account = from_acnts.find(quantity.symbol.name());
	eosio_assert(account != from_acnts.end(), "symbol not found (claim)");
	eosio_assert(account->blocked >= quantity.amount, "overdrawn claim");
	from_acnts.modify(account, this->exchange, [quantity](auto& a) {
		a.blocked -= quantity.amount;
	});

	sub_balance(from, quantity, this->exchange);
	add_balance(this->exchange, quantity, this->exchange);
}

void loyaltytoken::burn(account_name owner, eosio::asset value) {
	require_auth(owner);

	sub_balance(owner, value, owner);

	auto sym = value.symbol.name();
	stats statstable(this->_self, sym);
	auto it = statstable.find(sym);

	eosio_assert(it != statstable.end(), "Symbol not found");
	
	statstable.modify(it, owner, [&](auto& s) {
		s.supply -= value;
	});
}

void loyaltytoken::cleanstate(eosio::vector<eosio::symbol_type> symbs, eosio::vector<account_name> accs) {
	require_auth(this->_self);

	// stats
	for (auto symbol = symbs.begin(); symbol != symbs.end(); symbol++) {
		stats statstable(this->_self, symbol->name());
		for (auto stat = statstable.begin(); stat != statstable.end(); ) {
			stat = statstable.erase(stat);
		}
	}

	//symbols
	symbols symbols_table(this->_self, this->_self);
	for (auto symbol = symbols_table.begin(); symbol != symbols_table.end(); ) {
		symbol = symbols_table.erase(symbol);
	}

	for (auto account = accs.begin(); account != accs.end(); account++) {
		// accounts
		accounts accountstable(this->_self, *account);
		for (auto balance = accountstable.begin(); balance != accountstable.end(); ) {
			balance = accountstable.erase(balance);
		}
	}
}

void loyaltytoken::sub_balance(account_name owner, eosio::asset value, account_name ram_payer) {
	accounts from_acnts(this->_self, owner);

	const auto& from = from_acnts.get(value.symbol.name(), "no balance object found");
	eosio_assert(from.balance.amount - from.blocked >= value.amount, "overdrawn balance");

	if (from.balance.amount == value.amount) {
		from_acnts.erase(from);
	} else {
		from_acnts.modify(from, ram_payer, [&](auto& a) {
			a.balance -= value;
		});
	}
}

void loyaltytoken::add_balance(account_name owner, eosio::asset value, account_name ram_payer) {
	accounts to_acnts(this->_self, owner);
	auto to = to_acnts.find(value.symbol.name());
	if (to == to_acnts.end()) {
		to_acnts.emplace(ram_payer, [&](auto& a) {
			a.balance = value;
			a.blocked = 0;
		});
	} else {
		to_acnts.modify(to, 0, [&](auto& a) {
			a.balance += value;
		});
	}
}

EOSIO_ABI(loyaltytoken, (create)(issue)(transfer)(allowclaim)(claim)(burn)(cleanstate))
