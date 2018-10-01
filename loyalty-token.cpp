#include "loyalty-token.hpp"
#include "config.h"
#include "str_expand.h"

loyaltytoken::loyaltytoken(account_name self) :
	eosio::contract(self),
	exchange(eosio::string_to_name(STR(EXCHANGE))),
	state_singleton(this->_self, this->_self),
	clean(false),
	state(state_singleton.exists() ? state_singleton.get() : default_parameters())
{}

loyaltytoken::~loyaltytoken() {
	if (this->clean) {
		this->state_singleton.remove();
	} else {
		this->state_singleton.set(this->state, this->_self);
	}
}

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

void loyaltytoken::allowclaim(account_name from, account_name to, eosio::asset quantity) {
	require_auth(from);
	require_auth(this->exchange);

	require_recipient(from);
	require_recipient(to);

	accounts from_acnts(this->_self, from);
	const auto& account = from_acnts.find(quantity.symbol.name());
	eosio_assert(account != from_acnts.end(), "symbol not found");
	from_acnts.modify(account, from, [quantity](auto& a) {
		a.blocked += quantity.amount;
	});

	claims from_claims(this->_self, from);
	const auto& claim = from_claims.find(((uint128_t)to << 64) + quantity.symbol);
	if (claim == from_claims.end()) {
		from_claims.emplace(from, [to, quantity](auto& c) {
			c.to = to;
			c.quantity = quantity;
		});
	} else {
		if (claim->quantity.amount == -quantity.amount) {
			from_claims.erase(claim);
		} else {
			from_claims.modify(claim, from, [quantity](auto& c) {
				c.quantity += quantity;
			});
		}
	}
}

void loyaltytoken::claim(account_name from, account_name to, eosio::asset quantity) {
	require_auth(to);
	require_auth(this->exchange);

	eosio_assert(quantity.amount > 0, "claim must be positive");

	accounts from_acnts(this->_self, from);
	const auto& account = from_acnts.find(quantity.symbol.name());
	eosio_assert(account != from_acnts.end(), "symbol not found");
	from_acnts.modify(account, to, [quantity](auto& a) {
		a.blocked -= quantity.amount;
	});

	claims from_claims(this->_self, from);
	const auto& claim = from_claims.find(((uint128_t)to << 64) + quantity.symbol);
	eosio_assert(claim != from_claims.end(), "no available claim");
	if (claim->quantity.amount == quantity.amount) {
		from_claims.erase(claim);
	} else {
		from_claims.modify(claim, to, [quantity](auto& c) {
			c.quantity -= quantity;
		});
	}

	sub_balance(from, quantity, to);
	add_balance(to, quantity, to);
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

void loyaltytoken::cleanstate() {
	require_auth(this->_self);
	this->clean = true;
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

EOSIO_ABI(loyaltytoken, (create)(issue)(allowclaim)(claim)(burn)(cleanstate))
