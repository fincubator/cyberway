#include "custom_unpack.hpp"
#include "genesis_create.hpp"
#include "state_reader.hpp"
#include "golos_objects.hpp"
#include "serializer.hpp"
#include "genesis_generate_name.hpp"
#include <eosio/chain/config.hpp>
#include <eosio/chain/authorization_manager.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/stake_object.hpp>
#include <eosio/chain/int_arithmetic.hpp>
#include <cyberway/chain/cyberway_contract_types.hpp>
#include <fc/io/raw.hpp>
#include <fc/variant.hpp>
#include <boost/filesystem/path.hpp>

// can be useful for testnet to avoid reset of witnesses, who updated node after HF (have old vote_hardfork values)
// #define ONLY_CHECK_WITNESS_RUNNING_HF_VERSION

// suppose name generation is slower than flat_map access by idx
#define CACHE_GENERATED_NAMES

namespace fc { namespace raw {

template<typename T> void unpack(T& s, cyberway::golos::comment_object& c) {
    fc::raw::unpack(s, c.id);
    fc::raw::unpack(s, c.parent_author);
    fc::raw::unpack(s, c.parent_permlink);
    fc::raw::unpack(s, c.author);
    fc::raw::unpack(s, c.permlink);
    fc::raw::unpack(s, c.depth);
    fc::raw::unpack(s, c.children);
    fc::raw::unpack(s, c.mode);
    if (c.mode != cyberway::golos::comment_mode::archived) {
        fc::raw::unpack(s, c.active);
    }
}

}} // fc::raw


namespace cyberway { namespace genesis {

using namespace eosio::chain;
using namespace cyberway::chaindb;
using perm_id_t = authorization_manager::permission_id_type;


constexpr uint64_t permissions_tbl_start_id = 17;       // Note: usage_id is id-1


using eosio::chain::uint128_t;
uint64_t to_fbase(uint64_t value)   { return value << fixp_fract_digits; }
uint128_t to_fwide(uint128_t value) { return value << fixp_fract_digits; }

// Golos state holds uint128 as pair of uint64 values in Big Endian order: high, then low, incompatible with __int128
uint128_t fix_fc128(uint128_t x)    { return (x << 64) | (x >> 64); }

string pubkey_string(const golos::public_key_type& k);
public_key_type pubkey_from_golos(const golos::public_key_type& k) {
    // can't construct public key directly, constructor is private. transform to string first:
    return public_key_type(pubkey_string(k));
}
asset golos2sys(const asset& golos);

struct genesis_create::genesis_create_impl final {
    genesis_info _info;
    genesis_state _conf;
    contracts_map _contracts;

    genesis_serializer db;

    vector<string> _accs_map;
    vector<string> _plnk_map;

    state_object_visitor _visitor;
    bool _has_golos_state = false;
    asset _total_staked = asset();
    asset _sys_supply = asset();

    export_info _exp_info;

#ifdef CACHE_GENERATED_NAMES
    fc::flat_map<acc_idx, account_name> _idx2name;
    name name_by_idx(acc_idx idx) {
        if (_idx2name.count(idx) == 0) {
            _idx2name[idx] = generate_name(_accs_map[idx]);
        }
        return _idx2name[idx];
    }
#else
    name name_by_idx(acc_idx idx) {
        return generate_name(_accs_map[idx]);
    }
#endif
    name name_by_username(const std::string& username) {
        if (username[0] == '~') {
            auto itr = std::find(_accs_map.begin(), _accs_map.end(), username.substr(1,std::string::npos));
            EOS_ASSERT(itr != _accs_map.end(), genesis_exception, "User ${username} doesn't exist", ("username",username));
            return name_by_idx(itr - _accs_map.begin());
        } else {
            eosio::chain::name name{username};
            auto itr = std::find_if(_info.accounts.begin(), _info.accounts.end(), [&](const auto& acc){return acc.name == name;});
            EOS_ASSERT(itr != _info.accounts.end(), genesis_exception, "Account ${username} doesn't exist", ("username",username));
            return name;
        }
    }
    name name_by_acc(const golos::account_name_type& acc) {
        return name_by_idx(acc.id.value);
    }
    name name_by_id(golos::id_type id) {
        return name_by_idx(_visitor.acc_id2idx[id]);
    }

    supply_distributor get_gbg_to_golos_converter() {
        const auto& gp = _visitor.gpo;
        golos::price price;
        if (gp.is_forced_min_price) {
            // This price limits SBD to 10% market cap
            price = golos::price{asset(9 * gp.current_sbd_supply.get_amount(), symbol(GBG)), gp.current_supply};
        } else {
            EOS_ASSERT(false, genesis_exception, "Not implemented");
        }
        supply_distributor to_gls(price.quote, price.base);
        return to_gls;
    }

    genesis_create_impl() {
        db.set_autoincrement<permission_object>(permissions_tbl_start_id);
        db.set_autoincrement<permission_usage_object>(permissions_tbl_start_id-1);
    }

    template<typename T, typename Lambda>
    void store_accounts_wo_perms(const T& accounts, Lambda&& get_name) {
        auto l = accounts.size();
        const auto ts = _conf.initial_timestamp;
        fc::flat_set<name> updated_accs;

        db.start_section(config::system_account_name, N(account), "account_object", l);
        for (const auto& acc: accounts) {
            auto n = get_name(acc);
            db.emplace<account_object>(n, n, [&](auto& a) {
                a.creation_date = ts;
                if (_contracts.count(n) > 0) {
                    const auto& abicode = _contracts[n];
                    if (abicode.update) {
                        updated_accs.insert(n);
                    }
                    if (abicode.privileged) {
                        a.privileged = abicode.privileged;
                    }
                    if (abicode.abi.size()) {
                        a.abi = abicode.abi;
                        a.abi_version = abicode.abi_hash;
                    }
                    const auto& code = abicode.code;
                    if (code.size()) {
                        a.last_code_update = ts;
                        a.code_version = abicode.code_hash;
                        a.code.resize(code.size());
                        memcpy(const_cast<char*>(a.code.data()), code.data(), code.size());
                    }
                }
            });
        }
        l -= updated_accs.size();

        db.start_section(config::system_account_name, N(accountseq), "account_sequence_object", l);
        for (const auto& acc: accounts) {
            auto n = get_name(acc);
            if (updated_accs.count(n) == 0) {
                db.emplace<account_sequence_object>(n, n, [&](auto& a) {});
            }
        }

        db.start_section(config::system_account_name, N(resusage), "resource_usage_object", l);
        for (const auto& acc: accounts) {
            auto n = get_name(acc);
            if (updated_accs.count(n) == 0) {
                db.emplace<resource_usage_object>(n, n, [&](auto& u) {
                    u.accumulators.resize(resource_limits::resources_num);
                });
            }
        }
    }

    const permission_object store_permission(
        account_name account, permission_name name, perm_id_t parent, authority auth, uint64_t usage_id
    ) {
        const auto perm = db.emplace<permission_object>(account, [&](auto& p) {
            p.usage_id     = usage_id;  // perm_usage.id;
            p.parent       = parent;
            p.owner        = account;
            p.name         = name;
            p.last_updated = _conf.initial_timestamp;
            p.auth         = std::move(auth);
        });
        return perm;
    }

    void parse_and_store_permissions(account_name n, const std::vector<genesis_info::permission>& permissions, uint64_t& id, std::map<permission_name,perm_id_t>& parents, const public_key_type& initial_key) {
        const uint64_t max_custom_parent_id = 100;            // this should be enough to cover build-in permissions
        for (const auto& p: permissions) {
            const auto& auth = p.make_authority(initial_key, n, [&](const std::string& username){return name_by_username(username);});
            auto parent = p.get_parent();
            bool root = parent == eosio::chain::name();
            bool custom_parent = !root && parent.value < max_custom_parent_id;
            EOS_ASSERT(root || custom_parent || parents.count(parent) > 0, genesis_exception,
                    "Parent ${pa} not found for permission ${p} of account ${a}", ("a",n)("p",p.name)("pa",p.parent));
            perm_id_t parent_id = root ? perm_id_t(0) : custom_parent ? perm_id_t(parent.value) : parents[parent];
            const auto& perm = store_permission(n, p.name, parent_id, auth, id++);
            parents[p.name] = perm.id;
        }
    }

    void store_contracts() {
        ilog("Creating contracts...");
        store_accounts_wo_perms(_contracts, [](auto& a){ return a.first; });

        int perms_l = 0;
        for (const auto& acc: _info.accounts) {
            perms_l += acc.permissions.size();
        }

        // we can't store records from different tables simultaneously, so save current autoincrement id to use later
        uint64_t usage_id = db.get_autoincrement<permission_usage_object>();
        db.start_section(config::system_account_name, N(permusage), "permission_usage_object", perms_l);
        for (const auto& acc: _info.accounts) {
            for (int i = 0, l = acc.permissions.size(); i < l; i++) {
                db.emplace<permission_usage_object>(acc.name, [&](auto& p) {
                    p.last_used = _conf.initial_timestamp;
                });
            }
        }

        db.start_section(config::system_account_name, N(permission), "permission_object", perms_l);
        uint64_t max_custom_parent_id = 100;            // this should be enough to cover build-in permissions
        public_key_type system_key(_conf.initial_key);
        for (const auto& acc: _info.accounts) {
            std::map<permission_name,perm_id_t> parents;
            parse_and_store_permissions(acc.name, acc.permissions, usage_id, parents, _conf.initial_key);
        }
        ilog("Done.");
    }

    void store_auth_links() {
        ilog("Link contracts' authtorities...");
        int n_links = 0;
        for (const auto& auth: _info.auth_links) {
            n_links += auth.links.size();
        }

        db.start_section(config::system_account_name, N(permlink), "permission_link_object", n_links);
        for (const auto& auth: _info.auth_links) {
            const auto perm = auth.get_permission();
            const auto links = auth.get_links();
            for (const auto& l: links) {
                db.emplace<permission_link_object>(perm.first, [&](auto& link) {
                    link.account = perm.first;
                    link.code = l.first;
                    link.message_type = l.second;
                    link.required_permission = perm.second;
                });
            }
        }
        ilog("Done.");
    }

    void store_custom_tables() {
        ilog("Store custom tables...");
        for (const auto& tbl: _info.tables) {
            db.start_section(tbl.code, tbl.table, tbl.abi_type, tbl.rows.size());
            for (const auto& r: tbl.rows) {
                db.insert(r.pk, r.get_scope(), r.payer, r.data);
            }
        }
        ilog("Done.");
    }

    void store_accounts() {
        ilog("Creating accounts, authorities and usernames in Golos domain...");

#ifndef CACHE_GENERATED_NAMES
        // first fill names, we need them to check is authority account exists
        fc::flat_map<acc_idx, bool> _idx2name;
        for (const auto a: _visitor.auths) {
            _idx2name[a.account.id.value] = true;
        }
#endif

        store_accounts_wo_perms(_visitor.auths, [&](auto& a) {
            return name_by_acc(a.account);
        });

        // fill auths
        uint64_t added_perms = 0;
        for (auto& a: _info.transit_account_authorities) {
            EOS_ASSERT(a.username[0] == '~', genesis_exception, "Only transit users allowed");
            added_perms += a.permissions.size();
            a.name = name_by_username(a.username);
        }

        uint64_t usage_id = db.get_autoincrement<permission_usage_object>();
        const int permissions_per_account = 3;          // owner+active+posting
        const auto perms_l = _visitor.auths.size() * permissions_per_account + added_perms;
        db.start_section(config::system_account_name, N(permusage), "permission_usage_object", perms_l);
        for (const auto a: _visitor.auths) {
            const auto n = name_by_acc(a.account);
            uint64_t added = 0;
            auto itr = std::find_if(_info.transit_account_authorities.begin(), _info.transit_account_authorities.end(),
                    [&](const auto& acc) {return acc.name == n;});
            if (itr != _info.transit_account_authorities.end()) {
                added = itr->permissions.size();
            }
            for (int i = 0; i < permissions_per_account + added; i++) {
                db.emplace<permission_usage_object>(n, [&](auto& p) {
                    p.last_used = _conf.initial_timestamp;
                });
            }
        }

        db.start_section(config::system_account_name, N(permission), "permission_object", perms_l);
        for (const auto a: _visitor.auths) {
            const auto n = a.account.str(_accs_map);
            auto convert_authority = [&](permission_name perm, const golos::shared_authority& a, name recovery = {}) {
                bool recoverable = recovery != name() && a.weight_threshold == 1 && a.account_auths.size() == 0;
                uint32_t threshold = recoverable ? 3 : n == "temp" ? 1 : a.weight_threshold;
                vector<key_weight> keys;
                for (const auto& k: a.key_auths) {
                    keys.emplace_back(key_weight{pubkey_from_golos(k.first), recoverable ? weight_type{2} : k.second});
                }
                vector<permission_level_weight> accounts;
                for (const auto& p: a.account_auths) {
                    auto acc = p.first;
                    if (_idx2name.count(acc.id.value)) {
                        accounts.emplace_back(permission_level_weight{{name_by_acc(acc), perm}, p.second});
                    } else {
                        ilog("Note: account ${a} tried to add unexisting account ${b} to it's ${p} authority. Skipped.",
                            ("a",n)("b",acc.str(_accs_map))("p",perm));
                    }
                }
                std::vector<wait_weight> waits;
                if (recoverable) {
                    accounts.emplace_back(permission_level_weight{{recovery, config::owner_name}, 1});
                    waits.emplace_back(wait_weight{_info.golos.recovery.wait_days*60*60*24, 1});
                }
                return authority{threshold, keys, accounts, waits};
            };
            auto recovery_acc = name_by_acc(_visitor.accounts[a.account.id].recovery_account);
            const auto own = convert_authority(config::owner_name, a.owner, recovery_acc);
            const auto act = convert_authority(config::active_name, a.active);
            const auto post = convert_authority(posting_auth_name, a.posting);
            _exp_info.account_infos[a.account.id] = mvo
                ("owner_auth", own)
                ("active_auth", act)
                ("posting_auth", post);

            auto name = name_by_acc(a.account);
            const auto& owner  = store_permission(name, config::owner_name, 0, own, usage_id++);
            const auto& active = store_permission(name, config::active_name, owner.id, act, usage_id++);
            const auto& posting= store_permission(name, posting_auth_name, active.id, post, usage_id++);

            auto itr = std::find_if(_info.transit_account_authorities.begin(), _info.transit_account_authorities.end(),
                    [&](const auto& acc) {return acc.name == name;});
            if (itr != _info.transit_account_authorities.end()) {
                std::map<permission_name,perm_id_t> parents = {
                        {config::owner_name, owner.id},
                        {config::active_name, active.id},
                        {posting_auth_name, posting.id}
                };
                parse_and_store_permissions(name, itr->permissions, usage_id, parents, _conf.initial_key);
            }
        }

        // link posting permission with gls.publish and gls.social
        db.start_section(config::system_account_name, N(permlink), "permission_link_object", _visitor.auths.size()*2);
        auto insert_link = [&](name acc, name code) {
            db.emplace<permission_link_object>(acc, [&](auto& link) {
                link.account = acc;
                link.code = code;
                link.message_type = name();
                link.required_permission = N(posting);
            });
        };
        for (const auto a: _visitor.auths) {
            const auto n = name_by_acc(a.account);
            insert_link(n, _info.golos.names.posting);
            insert_link(n, _info.golos.names.social);
        }

        // add usernames
        db.start_section(config::system_account_name, N(domain), "domain_object", 1);
        const auto app = _info.golos.names.issuer;
        db.emplace<domain_object>(app, [&](auto& a) {
            a.owner = app;
            a.linked_to = app;
            a.creation_date = _conf.initial_timestamp;
            a.name = _info.golos.domain;
        });

        db.start_section(config::system_account_name, N(username), "username_object", _visitor.auths.size());
        for (const auto& auth : _visitor.auths) {                // loop through auths to preserve names order
            const auto& s = auth.account.str(_accs_map);
            const auto& n = name_by_acc(auth.account);
            const auto& acc = _visitor.accounts[auth.account.id];
            db.emplace<username_object>(app, [&](auto& u) {
                u.owner = n;
                u.scope = app;
                u.name = s;
            });
            _exp_info.account_infos[auth.account.id] = _exp_info.account_infos[auth.account.id]
                ("creator", app)
                ("owner", n)
                ("name", s)
                ("created", acc.created)
                ("last_update", acc.last_account_update)
                ("reputation", _visitor.reputations[auth.account.id]);
        }

        _visitor.auths.clear();
        ilog("Done.");
    }

    void store_stakes() {
        ilog("Creating staking agents and grants...");
        EOS_ASSERT(_total_staked != asset(), genesis_exception, "SYSTEM: _total_staked not initialized");
        EOS_ASSERT(_sys_supply != asset(), genesis_exception, "SYSTEM: _sys_supply not initialized");
        const auto sys_sym = asset().get_symbol();
        const auto inf = _info.params.stake;

        db.start_section(config::system_account_name, N(stake.param), "stake_param_object", 1);
        db.emplace<stake_param_object>(config::system_account_name, [&](auto& p) {
            p.id = sys_sym.to_symbol_code().value;
            p.token_symbol = sys_sym;
            p.max_proxies = inf.max_proxies;
            p.min_own_staked_for_election = inf.min_own_staked_for_election;
            p.depriving_window = inf.depriving_window;
        });

        // first prepare staking balances and sort agents by level. keys and proxy info required to do this
        fc::flat_map<acc_idx,public_key_type> keys;         // agent:key
        std::vector<producer_key> initial_producers = get_producers();
        for (const auto& w: _visitor.witnesses) {
            auto producer = std::find_if(initial_producers.begin(), initial_producers.end(), [&](const auto &prod) {return prod.producer_name == name_by_acc(w.owner);});
            keys[w.owner.id.value] = (producer == initial_producers.end()) ? public_key_type{} : producer->block_signing_key;
        }
        fc::flat_map<acc_idx,acc_idx> proxies;              // grantor:agent
        const auto& empty_acc = std::distance(_accs_map.begin(), std::find(_accs_map.begin(), _accs_map.end(), string("")));
        for (const auto& a: _visitor.accounts) {
            const auto& proxy = a.second.proxy.id.value;
            if (proxy != empty_acc) {
                proxies[a.first] = proxy;
            }
        }
        std::map<name,string> names;
        struct agent {
            account_name name;
            uint8_t level;
            int64_t balance;
            int64_t proxied;
            int64_t own_share;
            int64_t shares_sum;

            int64_t own_staked() const {
                return int_arithmetic::safe_prop(balance + proxied, own_share, shares_sum);
            }
        };
        using agents_map = fc::flat_map<acc_idx,agent>;
        std::vector<agents_map> agents_by_level{_info.params.stake.max_proxies.size() + 1};
        auto find_proxy_level = [&](acc_idx a) {
            uint8_t l = 0;
            if (keys.count(a)) {
                return l;           // BP
            }
            l++;
            while (l <= 4 && proxies.count(a) > 0) {
                a = proxies[a];
                l++;
            }
            return l;
        };

        std::cout << "  SYSTEM staked = " << _total_staked << std::endl;
        supply_distributor to_sys(_total_staked, _visitor.gpo.total_vesting_shares);
        asset staked(0);
        for (const auto& v: _visitor.vests) {
            auto acc = v.first;
            auto amount = to_sys.convert(asset(v.second.vesting, symbol(VESTS)));
            staked += amount;
            auto s = amount.get_amount();
            agent x{name_by_idx(acc), find_proxy_level(acc), s, 0, s, s};
            agents_by_level[x.level].emplace(acc, std::move(x));
            names[x.name] = _accs_map[acc];
        }
        std::cout << "  actual staked = " << staked << std::endl;
        std::cout << "    diff staked = " << (_total_staked - staked) << std::endl;
        EOS_ASSERT(_total_staked == staked, genesis_exception, "Staking supply differs from sum of balances");

        auto find_agent_level = [&](acc_idx acc, int upper_level) {
            do {
                upper_level--;
            } while (upper_level > 0 && agents_by_level[upper_level].count(acc) == 0);
            return upper_level;
        };

        // now it's possible to fill grants while moving by agents from largest level to 0
        struct grant_info {
            account_name from;
            account_name to;
            int16_t pct;
            int64_t granted;
        };
        std::vector<grant_info> grants;
        auto grant = [&](agent& from, agent& to, int64_t balance, int16_t pct) {
            from.balance -= balance;
            from.proxied += balance;
            to.balance += balance;
            to.shares_sum += balance;
            grants.emplace_back(grant_info{from.name, to.name, pct, balance});
        };

        // process proxies first (levels 4-2) and direct votes later (level 1); level 0 have no grants, so skip it
        for (int l = agents_by_level.size() - 1; l > 0; l--) {
            auto& agents = agents_by_level[l];
            for (auto& ag: agents) {
                auto acc = ag.first;
                auto& a = ag.second;
                if (proxies.count(acc) > 0) {
                    auto proxy = proxies[acc];
                    auto proxy_lvl = find_agent_level(proxy, l);
                    if (proxy_lvl >= 0) {
                        grant(a, agents_by_level[proxy_lvl][proxy], a.balance, config::percent_100);
                    } else {
                        wlog("Proxy of ${a} not found from level ${l}", ("l",l-1)("a",names[a.name]));
                    }
                } else {
                    if (l == 1) {
                        // direct votes at this level
                        const int max_votes = 30;
                        const int16_t pct = config::percent_100 / max_votes;
                        const auto& votes = _visitor.witness_votes[acc];
                        auto& bps = agents_by_level[0];
                        auto part = a.balance / max_votes;
                        int n = 0;
                        for (const auto& v: votes) {
                            bool can_vote = bps.count(v) > 0;
                            if (can_vote) {
                                n++;
                                bool last = n == max_votes;
                                grant(a, bps[v], last ? a.balance : part, pct + (last ? config::percent_100 % max_votes : 0));
                            } else {
                                wlog("Skipping ${a} vote for ${w} (not BP)", ("a",names[a.name])("w",_accs_map[v]));
                            }
                        }
                        EOS_ASSERT(n <= _info.params.stake.max_proxies[0], genesis_exception,
                            "configured max_proxies for level 1 = ${m} is less then actual number votes imported ${a}",
                            ("m",_info.params.stake.max_proxies[0])("a",n));
                    } else {
                        elog("No proxy for level ${l} (${a})", ("l",l)("a",names[a.name]));
                    }
                }
            }
        }
        db.start_section(config::system_account_name, N(stake.grant), "stake_grant_object", grants.size());
        for (const auto& g: grants) {
            db.emplace<stake_grant_object>(g.from, [&](auto& o) {
                o.token_code = sys_sym.to_symbol_code(),
                o.grantor_name = g.from,
                o.recipient_name = g.to,
                o.pct = g.pct,
                o.share = g.granted;
                o.break_fee = config::percent_100;
                o.break_min_own_staked = 0;
            });
        }

        struct delegate {
            bool used = false;
            uint64_t provided = 0;
            uint64_t received = 0;
        };
        std::map<name,delegate> delegatemap;
        uint64_t delegateid = 0;
        db.start_section(config::stake_account_name, N(provision), "provision_struct", _info.delegateuse.size());
        for (const auto& item: _info.delegateuse) {
            EOS_ASSERT(item.from != item.to, genesis_exception, "From and to can't be equal");
            EOS_ASSERT(1 == std::count_if(_info.delegateuse.begin(), _info.delegateuse.end(), [&](const auto& dlg) {return dlg.from == item.from && dlg.to == item.to;}),
                    genesis_exception, "Duplicate delegateuse item with ${from}:${to}", ("from", item.from)("to", item.to));
            name from = name_by_username(item.from);
            name to = name_by_username(item.to);
            asset quantity;
            if (item.quantity[item.quantity.length()-1] == '%') {
                int64_t percent = boost::lexical_cast<float>(item.quantity.substr(0, item.quantity.length()-1))*100;
                if (item.from[0] == '~') {
                    bool found = false;
                    for (const auto& abl: agents_by_level) {
                        for (auto& ag: abl) {
                            auto& acc = ag.second;
                            if (acc.name != from) continue;
                            auto total_funds = acc.balance + acc.proxied;
                            if (total_funds != 0) {
                                auto own_funds = int_arithmetic::safe_prop(total_funds, acc.own_share, acc.shares_sum);
                                quantity += asset(int_arithmetic::safe_pct(own_funds, percent));
                            }
                            found = true;
                            break;
                        }
                        if (found) break;
                    }
                } else {
                    auto acc = std::find_if(_info.accounts.begin(), _info.accounts.end(), [&](const auto& a){return a.name == from;});
                    EOS_ASSERT(acc != _info.accounts.end(), genesis_exception, "Can't find account ${name}", ("name", from));
                    EOS_ASSERT(acc->sys_staked, genesis_exception, "Account ${name} doesn't has staked funds", ("name", from));
                    quantity += asset(int_arithmetic::safe_pct(acc->sys_staked->get_amount(), percent));
                }
            } else {
                quantity += asset::from_string(item.quantity);
            }
            EOS_ASSERT(quantity.get_amount() > 0, genesis_exception, "Delegateuse ${from}:${to} provide empty funds (${quantity})",
                    ("from", item.from)("to", item.to)("quantity", item.quantity));
            delegatemap[from].provided += quantity.get_amount();
            delegatemap[to  ].received += quantity.get_amount();
            db.insert(delegateid, config::stake_account_name, mvo()
                ("id", delegateid)
                ("token_code", sys_sym.to_symbol_code())
                ("grantor_name", from)
                ("recipient_name", to)
                ("amount", (int64_t)quantity.get_amount()));
            delegateid++;
        }

        uint64_t n_delegate_agents = delegatemap.size() -
                std::count_if(_visitor.vests.begin(), _visitor.vests.end(), [&](const auto& v){return delegatemap.count(name_by_idx(v.first));}) -
                std::count_if(_info.accounts.begin(), _info.accounts.end(), [&](const auto& a){return a.sys_staked && delegatemap.count(a.name);});

        auto get_delegated = [&](name n) {
            auto itr = delegatemap.find(n);
            return itr != delegatemap.end() ? (itr->second.used=true,itr->second) : delegate();
        };

        int64_t total_votes = 0;
        auto n_acc_agents = std::count_if(_info.accounts.begin(), _info.accounts.end(), [](const auto& acc){return acc.sys_staked;});
        db.start_section(config::system_account_name, N(stake.agent), "stake_agent_object", _visitor.vests.size() + n_acc_agents + n_delegate_agents);
        for (const auto& abl: agents_by_level) {
            for (auto& ag: abl) {
                auto acc = ag.first;
                auto& x = ag.second;
                if (!x.level) {
                    total_votes += x.balance;
                }
                auto delegated = get_delegated(x.name);
                auto agent = db.emplace<stake_agent_object>(x.name, [&](auto& a) {
                    a.token_code = sys_sym.to_symbol_code();
                    a.account = x.name;
                    a.proxy_level = x.level;
                    a.last_proxied_update = _conf.initial_timestamp;
                    a.balance = x.balance;
                    a.proxied = x.proxied;
                    a.own_share = x.own_share;
                    a.shares_sum = x.shares_sum;
                    a.fee = config::percent_100;
                    a.min_own_staked = 0;
                    a.provided = delegated.provided;
                    a.received = delegated.received;
                });
                EOS_ASSERT(agent.get_own_funds() >= agent.provided, genesis_exception,
                        "Agent ${account} provide more funds (${provided}) than has (${funds})",
                        ("account", agent.account)("provided", agent.provided)("funds", agent.get_own_funds()));
                _exp_info.account_infos[acc] = _exp_info.account_infos[acc]
                    ("staked_balance", asset(x.balance, sys_sym))
                    ("staked_proxied", asset(x.proxied, sys_sym));
            }
        }
        for (const auto& acc: _info.accounts) {
            if (acc.sys_staked) {
                total_votes += acc.sys_staked->get_amount();
                auto delegated = get_delegated(acc.name);
                auto agent = db.emplace<stake_agent_object>(acc.name, [&](auto& a) {
                    a.token_code = sys_sym.to_symbol_code();
                    a.account = acc.name;
                    a.proxy_level = acc.prod_key ? 0 : 1;
                    a.last_proxied_update = _conf.initial_timestamp;
                    a.balance = acc.sys_staked->get_amount();
                    a.proxied = 0;
                    a.own_share = acc.sys_staked->get_amount();
                    a.shares_sum = acc.sys_staked->get_amount();
                    a.fee = config::percent_100;
                    a.min_own_staked = 0;
                    a.provided = delegated.provided;
                    a.received = delegated.received;
                });
                EOS_ASSERT(agent.get_own_funds() >= agent.provided, genesis_exception,
                        "Agent ${account} provide more funds (${provided}) than has (${funds})",
                        ("account", agent.account)("provided", agent.provided)("funds", agent.get_own_funds()));
            }
        }
        for (const auto& v: delegatemap) {
            if (v.second.used == false) {
                auto agent = db.emplace<stake_agent_object>(v.first, [&](auto& a) {
                    a.token_code = sys_sym.to_symbol_code();
                    a.account = v.first;
                    a.proxy_level = 1;
                    a.last_proxied_update = _conf.initial_timestamp;
                    a.balance = 0;
                    a.proxied = 0;
                    a.own_share = 0;
                    a.shares_sum = 0;
                    a.fee = config::percent_100;
                    a.min_own_staked = 0;
                    a.provided = v.second.provided;
                    a.received = v.second.received;
                });
                EOS_ASSERT(agent.get_own_funds() >= agent.provided, genesis_exception,
                        "Agent ${account} provide more funds (${provided}) than has (${funds})",
                        ("account", agent.account)("provided", agent.provided)("funds", agent.get_own_funds()));
            }
        }

        auto n_acc_cand = std::count_if(_info.accounts.begin(), _info.accounts.end(), [](const auto& acc){return acc.prod_key;});
        db.start_section(config::system_account_name, N(stake.cand), "stake_candidate_object", agents_by_level[0].size() + n_acc_cand);
        const auto sys_supply = _sys_supply.get_amount();
        for (auto& ag: agents_by_level[0]) {
            auto acc = ag.first;
            auto& x = ag.second;

            bool enabled = (x.own_staked() >= _info.params.stake.min_own_staked_for_election && keys.count(acc));
            db.emplace<stake_candidate_object>(x.name, [&](auto& a) {
                a.token_code = sys_sym.to_symbol_code();
                a.account = x.name;
                a.latest_pick = _conf.initial_timestamp;
                a.signing_key = enabled ? keys[acc] : public_key_type();
                a.enabled = enabled;
                a.set_votes(x.balance, sys_supply);
            });
        }
        public_key_type system_key(_conf.initial_key);
        for (const auto& acc: _info.accounts) {
            if (acc.prod_key) {
                auto balance = acc.sys_staked ? acc.sys_staked->get_amount() : 0;
                db.emplace<stake_candidate_object>(acc.name, [&](auto& a) {
                    a.token_code = sys_sym.to_symbol_code();
                    a.account = acc.name;
                    a.latest_pick = _conf.initial_timestamp;
                    a.signing_key = *acc.prod_key == "INITIAL" ? system_key : public_key_type(*acc.prod_key);
                    a.enabled = true;
                    a.set_votes(balance, sys_supply);
                });
            }
        }

        db.start_section(config::system_account_name, N(stake.stat), "stake_stat_object", 1);
        db.emplace<stake_stat_object>(config::system_account_name, [&](auto& s) {
            s.id = sys_sym.to_symbol_code().value;
            s.token_code = sys_sym.to_symbol_code();
            s.total_staked = _total_staked.get_amount();
            s.total_votes = total_votes;
            s.last_reward = time_point_sec();
            s.enabled = inf.enabled;
        });

        ilog("Done.");
    }

    void check_assets_invariants() {
        auto& data = _visitor;
        const auto& gp = data.gpo;
        std::cout << " Global Properties:" << std::endl;
        std::cout << "  current_supply = " << gp.current_supply << std::endl;
        std::cout << "  current_sbd_supply = " << gp.current_sbd_supply << std::endl;
        std::cout << "  total_vesting_shares = " << gp.total_vesting_shares << std::endl;
        std::cout << "  total_reward_fund_steem = " << gp.total_reward_fund_steem << std::endl;
        std::cout << "  total_vesting_fund_steem = " << gp.total_vesting_fund_steem << std::endl;

        std::cout << " GESTS total: " << data.total_gests << std::endl;
        auto total_idx = state_object_visitor::balance_type::_size;
        for (const auto& t: data.gbg_by_type) {
            auto& type = t.first;
            auto& gbg = t.second;
            auto& gls = data.gls_by_type[type];
            std::cout << std::setw(19) << std::left << (" " + data.balance_name(type) + ":")
                << std::setw(19) << std::right << gls << std::setw(17) << gbg << std::endl;
            if (type < total_idx) {
                data.gls_by_type[total_idx] += gls;
                data.gbg_by_type[total_idx] += gbg;
            }
        }
        auto gests_diff = gp.total_vesting_shares - data.total_gests;
        auto gls_diff = gp.current_supply -
            (gp.total_vesting_fund_steem + gp.total_reward_fund_steem + data.gls_by_type[total_idx]);
        auto gbg_diff = gp.current_sbd_supply - data.gbg_by_type[total_idx];
        EOS_ASSERT(gests_diff.get_amount() == 0 && gls_diff.get_amount() == 0 && gbg_diff.get_amount() == 0,
            genesis_exception,
            "Failed while check balances invariants", ("gests", gests_diff)("golos", gls_diff)("gbg", gbg_diff));
    }

    void store_balances() {
        ilog("Creating balances...");
        check_assets_invariants();

        auto& data = _visitor;
        const auto& gp = data.gpo;
        auto to_gls = get_gbg_to_golos_converter();
        auto golos_from_gbg = to_gls.convert(gp.current_sbd_supply);
        std::cout << "GBG to GOLOS = " << golos_from_gbg << std::endl;
        to_gls.reset();

        // token stats
        const auto n_stats = 2;
        db.start_section(config::token_account_name, N(stat), "currency_stats", n_stats);
        auto insert_stat_record = [&](const asset& supply, int64_t max_supply, name issuer) {
            const symbol& sym = supply.get_symbol();
            primary_key_t pk = sym.to_symbol_code();
            auto stat = mvo
                ("supply", supply)
                ("max_supply", asset(max_supply * sym.precision(), sym))
                ("issuer", issuer);
            db.insert(pk, pk, issuer, stat);
            _exp_info.currency_stats.push_back(stat);
            return pk;
        };

        auto total_acc_balances = asset();
        auto total_acc_staked = asset();
        for (const auto& acc: _info.accounts) {
            if (acc.sys_balance) {
                total_acc_balances += *acc.sys_balance;
            }
            if (acc.sys_staked) {
                total_acc_staked += *acc.sys_staked;
            }
        }

        _total_staked = golos2sys(gp.total_vesting_fund_steem);
        auto supply = gp.current_supply + golos_from_gbg;
        auto base = golos2sys(supply - gp.total_reward_fund_steem) + total_acc_balances + total_acc_staked;
        auto sys_supply = base.get_amount();
        for (const auto& fund: _info.params.funds) {
            sys_supply += int_arithmetic::safe_prop(base.get_amount(), fund.numerator, fund.denominator);
        }
        _sys_supply = asset(sys_supply, base.get_symbol());
        auto sys_pk = insert_stat_record(_sys_supply, _info.golos.sys_max_supply, config::system_account_name);
        auto gls_pk = insert_stat_record(supply, _info.golos.max_supply, _info.golos.names.issuer);

        // vesting info
        db.start_section(_info.golos.names.vesting, N(stat), "vesting_stats", 1);
        primary_key_t vests_pk = VESTS >> 8;
        auto vesting_stat = mvo
            ("supply", asset(data.total_gests.get_amount(), symbol(VESTS)));
        db.insert(vests_pk, _info.golos.names.vesting, vesting_stat
            ("notify_acc", _info.golos.names.control)
        );
        _exp_info.vesting_supply = vesting_stat;

        // funds
        const auto n_acc_balances = std::count_if(_info.accounts.begin(), _info.accounts.end(), [](const auto& a) {return a.sys_balance;}); //
        const auto n_balances = 3 + 2*data.gbg.size() + n_acc_balances + _info.params.funds.size();
        db.start_section(config::token_account_name, N(accounts), "account", n_balances);
        auto insert_balance_record = [&](name account, const asset& balance, primary_key_t pk,
            fc::optional<acc_idx> acc_id = {}
        ) {
            auto record = mvo
                ("balance", balance)
                ("payments", asset(0, balance.get_symbol()));
            db.insert(pk, account, record);
            if (!acc_id) {
                _exp_info.balance_events.push_back(record("account", account));
            } else {
                auto& acc_info = _exp_info.account_infos[*acc_id];
                acc_info[pk == gls_pk ? "balance" : "balance_in_sys"] = balance;
            }
        };
        insert_balance_record(config::stake_account_name, _total_staked + total_acc_staked, sys_pk);
        insert_balance_record(_info.golos.names.vesting, gp.total_vesting_fund_steem, gls_pk);
        insert_balance_record(_info.golos.names.posting, gp.total_reward_fund_steem, gls_pk);

        for (const auto& fund: _info.params.funds) {
            auto value = int_arithmetic::safe_prop(base.get_amount(), fund.numerator, fund.denominator);
            insert_balance_record(fund.name, asset(value, base.get_symbol()), sys_pk);
        }

        for (const auto& acc: _info.accounts) {
            if (acc.sys_balance) {
                insert_balance_record(acc.name, *acc.sys_balance, sys_pk);
            }
        }

        // accounts GOLOS
        asset total_gls = asset(0, symbol(GLS));
        for (const auto& balance: data.gbg) {
            auto acc = balance.first;
            auto gbg = balance.second;
            auto gls = to_gls.convert(gbg);
            _visitor.conv_gbg[acc].converted(gls);
            _visitor.conv_gls[acc].finish();
            gls += data.gls[acc];
            total_gls += gls;
            auto n = name_by_idx(acc);
            insert_balance_record(n, gls, gls_pk, acc);
            insert_balance_record(n, golos2sys(gls), sys_pk, acc);
            auto& acc_info = _exp_info.account_infos[acc];
            acc_info["vesting_shares"] = asset(data.vests[acc].vesting, symbol(VESTS));
            acc_info["received_vesting_shares"] = asset(data.vests[acc].received, symbol(VESTS));
        }
        const auto liquid_supply = supply - (gp.total_vesting_fund_steem + gp.total_reward_fund_steem); // no funds
        std::cout << " Total sum of GOLOS + converted GBG = " << total_gls
            << "; diff = " << (liquid_supply - total_gls) << std::endl;
        EOS_ASSERT(liquid_supply == total_gls, genesis_exception, "GOLOS supply differs from sum of balances");

        // accounts GESTS
        db.start_section(_info.golos.names.vesting, N(accounts), "account", data.vests.size());
        for (const auto& v: data.vests) {
            auto acc = v.first;
            const auto& vests = v.second;
            EOS_ASSERT(vests.delegated == data.delegated_vests[acc], genesis_exception,
                "Calculated delegated vests doesn't equal to provided in object",
                ("account", _accs_map[acc])("calculated", data.delegated_vests[acc])("provided", vests.delegated));
            EOS_ASSERT(vests.received == data.received_vests[acc], genesis_exception,
                "Calculated received delegations doesn't equal to provided in object",
                ("account", _accs_map[acc])("calculated", data.received_vests[acc])("provided", vests.received));
            db.insert(vests_pk, name_by_idx(acc), mvo
                ("vesting", asset(vests.vesting, symbol(VESTS)))
                ("delegated", asset(vests.delegated, symbol(VESTS)))
                ("received", asset(vests.received, symbol(VESTS)))
                ("unlocked_limit", asset(0, symbol(VESTS)))
                ("delegators", data.delegators[acc])
            );
        }
        ilog("Done.");
    }

    void store_delegation_records() {
        ilog("Creating vesting delegation records...");

        db.start_section(_info.golos.names.vesting, N(delegation), "delegation", _visitor.delegations.size());
        primary_key_t pk = 0;
        for (const auto& d: _visitor.delegations) {
            auto delegator = name_by_acc(d.delegator);
            auto delegation = mvo
                ("delegator", delegator)
                ("delegatee", name_by_acc(d.delegatee))
                ("quantity", asset(d.vesting_shares.get_amount(), symbol(VESTS)))
                ("interest_rate", d.interest_rate)
                ("min_delegation_time", d.min_delegation_time);
            db.insert(pk, VESTS >> 8, delegator, delegation("id", pk));
            pk++;
            _exp_info.delegations.push_back(delegation);
        }

        db.start_section(_info.golos.names.vesting, N(rdelegation), "return_delegation",
            _visitor.delegation_expirations.size());
        pk = 0;
        for (const auto& d: _visitor.delegation_expirations) {
            auto delegator = name_by_acc(d.delegator);
            db.insert(pk, _info.golos.names.vesting, delegator, mvo
                ("id", pk)
                ("delegator", delegator)
                ("quantity", asset(d.vesting_shares.get_amount(), symbol(VESTS)))
                ("date", d.expiration)
            );
            pk++;
        }
        ilog("Done.");
    }

    void store_withdrawals() {
        ilog("Creating vesting withdraw records...");
        const auto withdrawing_acc = [](const auto& a) {
            bool good = a.to_withdraw > 0 && a.vesting_withdraw_rate.get_amount() > 0 &&
                a.next_vesting_withdrawal != time_point_sec::maximum();
            if (good) {
                // some Golos accounts have low remainders due rounding (<0.001 GOLOS) or low withdraw rate,
                // both resulting wrong payments count. skip them
                auto payments_done = a.withdrawn / a.vesting_withdraw_rate.get_amount();
                good = payments_done >= 0 && payments_done < withdraw_intervals;
            }
            return good;
        };

        const auto& accs = _visitor.accounts;
        auto n = std::count_if(accs.begin(), accs.end(), [&](const auto& a){ return withdrawing_acc(a.second); });
        db.start_section(_info.golos.names.vesting, N(withdrawal), "withdrawal", n);

        const auto& routes = _visitor.withdraw_routes;
        for (const auto& acc: accs) {
            auto a = acc.second;
            if (withdrawing_acc(a)) {
                auto idx = acc.first;
                auto owner = name_by_idx(idx);
                auto to = routes.count(idx) ? name_by_idx(routes.at(idx)) : owner;
                auto remaining = a.to_withdraw - a.withdrawn;
                EOS_ASSERT(remaining <= a.vesting_shares.get_amount() - a.delegated_vesting_shares.get_amount(),
                    genesis_exception, "${a} trying to withdraw more vesting than allowed", ("a",_accs_map[idx]));
                db.insert(owner, VESTS >> 8, owner, mvo
                    ("owner", owner)
                    ("to", to)
                    ("interval_seconds", withdraw_interval_seconds)
                    ("remaining_payments", withdraw_intervals - a.withdrawn / a.vesting_withdraw_rate.get_amount())
                    ("next_payout", a.next_vesting_withdrawal)
                    ("withdraw_rate", asset(a.vesting_withdraw_rate.get_amount(), symbol(VESTS)))
                    ("to_withdraw", asset(remaining, symbol(VESTS)))
                );
            }
        }
        ilog("Done.");
    }

    void store_witnesses() {
        ilog("Creating witnesses...");
        fc::flat_map<acc_idx,int64_t> weights;  // accumulate weights per witness to compare with witness.total_weight
        fc::flat_map<acc_idx,uint64_t> vote_counts;  // accumulate votes counts per witness

        // Golos dApp have no proxy for witnesses, so create direct votes instead
        const auto& empty_acc = std::distance(_accs_map.begin(), std::find(_accs_map.begin(), _accs_map.end(), string("")));
        for (const auto& acc: _visitor.accounts) {
            auto idx = acc.first;
            const auto& a = acc.second;
            if (a.proxy.id.value != empty_acc) {
                bool found = false;
                auto final_proxy = a.proxy.id.value;
                for (int depth = 0; !found && depth < 4; depth++) {
                    const auto& proxy = _visitor.accounts[final_proxy].proxy.id.value;
                    found = proxy == empty_acc;
                    if (!found) {
                        final_proxy = proxy;
                    }
                }
                EOS_ASSERT(found, genesis_exception, "Account proxy depth >= 4");
                _visitor.witness_votes[idx];    // force create element so container won't change after obtainig final_proxy element
                _visitor.witness_votes[idx] = _visitor.witness_votes[final_proxy];
            }
        }

        // process votes before witnesses to calculate total weights
        db.start_section(_info.golos.names.control, N(witnessvote), "witness_voter", _visitor.witness_votes.size());
        for (const auto& v: _visitor.witness_votes) {
            const auto& acc = v.first;
            const auto n = name_by_idx(acc);
            const auto& votes = v.second;
            EOS_ASSERT(votes.size() <= 30, genesis_exception,
                "Account `${a}` have ${n} witness votes, but max 30 allowed", ("a",_accs_map[acc])("n",votes.size()));

            vector<account_name> witnesses;
            const auto& vests = _visitor.accounts[acc].vesting_shares.get_amount();
            for (const auto& w: votes) {
                witnesses.emplace_back(name_by_idx(w));
                weights[w] += vests;
                vote_counts[w]++;
                _exp_info.witness_votes[w].insert(n);
            }
            db.insert(n.value, _info.golos.names.control, n, mvo
                ("voter", n)
                ("witnesses", witnesses)
            );
        }

        db.start_section(_info.golos.names.control, N(witness), "witness_info", _visitor.witnesses.size());
        for (const auto& w : _visitor.witnesses) {
            const auto n = name_by_acc(w.owner);
            primary_key_t pk = n.value;
            auto witness = mvo
                ("name", n)
                ("url", w.url)
                ("active", true)
                ("total_weight", w.votes);
            db.insert(pk, _info.golos.names.control, n, witness
                ("counter_votes", vote_counts[w.owner.id])
            );
            _exp_info.witnesses[w.owner.id] = witness;

            if (weights[w.owner.id] != w.votes) {
                wlog("Witness `${a}` .votes value ${w} ≠ ${c}",
                    ("a",w.owner.str(_accs_map))("w",w.votes)("c",weights[w.owner.id]));
            }
            EOS_ASSERT(weights[w.owner.id] == w.votes, genesis_exception,
                "Witness .votes value is not equal to sum individual votes", ("w",w)("calculated",weights[w.owner.id]));
        }

        _visitor.witnesses.clear();
        _visitor.witness_votes.clear();
        ilog("Done.");
    }

    void store_posts() {
        ilog("Creating reward pool, posts & votes...");

        primary_key_t pool_date = 0;

        // first create lookup table to find author by post id
        fc::flat_map<uint64_t, name> authors;           // post_id:name
        for (const auto& c : _visitor.comments) {
            authors[c.second.id] = name_by_acc(c.second.author);
        }

        // store votes
        fc::flat_map<uint64_t, uint64_t> vote_weights_sum;
        db.start_section(_info.golos.names.posting, N(vote), "voteinfo", _visitor.votes.size());
        primary_key_t pk = 0;
        for (const auto& v: _visitor.votes) {
            std::vector<mvo> delegators;
            for (const auto& d: v.delegator_vote_interest_rates) {
                delegators.emplace_back(mvo
                    ("delegator", name_by_acc(d.account))
                    ("interest_rate", d.interest_rate)
                );
            }
            auto cid = v.comment;
            uint64_t w = v.orig_rshares <= 0 ? 0 : v.rshares;
            vote_weights_sum[cid] += w;
            if (v.num_changes != 0) {
                w = 0;
            } else if (w > 0) {
                uint64_t auction_window = _visitor.comments[cid].active.auction_window_size;
                if (v.auction_time != auction_window) {
                    w = int_arithmetic::safe_prop(w, static_cast<uint64_t>(v.auction_time), auction_window);
                }
            }
            auto vname = name_by_id(v.voter);
            db.insert(pk, authors[cid], vname, mvo
                ("id", pk)
                ("message_id", cid)
                ("voter", vname)
                ("weight", v.vote_percent)
                ("time", time_point(v.last_update).time_since_epoch().count())
                ("count", v.num_changes)
                ("delegators", delegators)
                ("curatorsw", w)
                ("rshares", v.rshares)
                ("paid_amount", 0)
            );
            pk++;
        }
        // store messages
        const int n = _visitor.comments.size();     // messages count
        db.start_section(_info.golos.names.posting, N(permlink), "permlink", _visitor.permlinks.size());
        auto key = [](acc_idx a, plk_idx p) { return uint64_t(a) << 32 | p; };
        std::unordered_map<uint64_t, uint64_t> post_ids;
        post_ids.reserve(_visitor.permlinks.size());
        for (const auto& cp : _visitor.permlinks) {
            const auto& c = cp.second;
            pk = cp.first;
            db.insert(pk, name_by_idx(c.author), mvo
                ("id", pk)
                ("parentacc", name_by_idx(c.parent_author))
                ("parent_id", post_ids[key(c.parent_author, c.parent_permlink)])
                ("value", _plnk_map[c.permlink])
                ("level", c.depth)
                ("childcount", c.children)
            );
            post_ids[key(c.author, c.permlink)] = pk;
        }
        post_ids.clear();
        _visitor.permlinks.clear();

        auto to_gls = get_gbg_to_golos_converter();
        db.start_section(_info.golos.names.posting, N(message), "message", n);
        uint128_t sum_net_rshares = 0;
        uint128_t sum_net_positive = 0;
        using beneficiary = std::pair<name,uint16_t>;
        for (const auto& cp : _visitor.comments) {
            const auto& c = cp.second;
            std::vector<beneficiary> beneficiaries;
            const auto sz = c.active.beneficiaries.size();
            if (sz) {
                beneficiaries.reserve(sz);
                for (const auto& b: c.active.beneficiaries) {
                    beneficiaries.emplace_back(beneficiary{name_by_acc(b.account), b.weight});
                }
            }
            pk = c.id;
            db.insert(pk, _info.golos.names.posting, mvo
                ("author", name_by_acc(c.author))
                ("id", pk)
                ("date", time_point(c.active.created).time_since_epoch().count())
                ("pool_date", pool_date)
                ("tokenprop", c.active.percent_steem_dollars / 2)
                ("beneficiaries", beneficiaries)
                ("rewardweight", c.active.reward_weight)
                ("state", mvo
                    ("netshares", c.active.net_rshares)
                    ("voteshares", c.active.vote_rshares)
                    ("sumcuratorsw", vote_weights_sum[c.id]))
                ("curators_prcnt", c.active.curation_rewards_percent)
                ("cashout_time", std::max(time_point(c.active.cashout_time), _conf.initial_timestamp).time_since_epoch().count())
                ("mssg_reward", asset(0, symbol(GLS)))
                ("max_payout", to_gls.convert(c.active.max_accepted_payout))
                ("paid_amount", 0)
            );
            sum_net_rshares += c.active.net_rshares;
            sum_net_positive += (c.active.net_rshares > 0) ? c.active.net_rshares : 0;
            if (c.mode != cyberway::golos::comment_mode::archived) {
                auto id = std::string(_accs_map[c.author.id]) + "/" + _plnk_map[c.permlink.id];
                auto hash = fc::hash64(id.c_str(), id.length());
                _exp_info.active_comments[hash] = c.active;
            }
        }
        // invariant
        const auto& gp = _visitor.gpo;
        uint128_t total_rshares = fix_fc128(gp.total_reward_shares2);
        EOS_ASSERT(total_rshares == sum_net_positive, genesis_exception,
            "GPO total rshares ${t} do not match to sum from posts ${s}", ("t", total_rshares)("s", sum_net_positive));

        // store pool
        pk = pool_date;
        db.start_section(_info.golos.names.posting, N(rewardpools), "rewardpool", 1);
        db.insert(pk, _info.golos.names.posting, mvo
            ("created", pk)
            ("rules", _info.params.posting_rules)
            ("state", mvo
                ("msgs", n)
                ("funds", gp.total_reward_fund_steem)
                ("rshares", sum_net_rshares)
                ("rsharesfn", total_rshares)
            )
        );

        _visitor.comments.clear();
        _visitor.votes.clear();
        ilog("Done.");
    }

    void store_bandwidth() {
        ilog("Creating bandwidth (charge) balances and posting limits...");
        // store charge balances
        enum charge_id_t: uint8_t {vote, post, comm, postbw};   // vote should have id=0
        const auto& accs = _visitor.accounts;
        db.start_section(_info.golos.names.charge, N(balances), "balance", accs.size()*2);
        const auto sym = symbol(GLS).to_symbol_code();
        const auto sname = symbol(GLS).name();
        for (const auto& ac : accs) {
            const auto& a = ac.second;
            auto insert_bw = [&](uint8_t charge_id, time_point_sec last_update, uint32_t value) {
                primary_key_t pk = symbol(charge_id, sname.c_str()).value();
                db.insert(pk, name_by_acc(a.name), mvo
                    ("charge_symbol", pk)
                    ("token_code", sym)
                    ("charge_id", charge_id)
                    ("last_update", time_point(last_update).time_since_epoch().count())
                    ("value", to_fbase(value))
                );
            };
            insert_bw(charge_id_t::vote, a.last_vote_time, config::percent_100 - a.voting_power);
            const auto& bw = _visitor.post_bws[a.name.id];
            insert_bw(charge_id_t::postbw, bw.second, bw.first);    // max_elapsed = 24h; maxbw=400%, maxw = maxbw^2
        }
        ilog("Done.");
    }

    void store_memo_keys() {
        ilog("Creating memo keys...");
        const auto& accs = _visitor.accounts;
        db.start_section(_info.golos.names.memo, N(memo), "memo_key", accs.size());
        for (const auto& ac : accs) {
            const auto& a = ac.second;
            const auto n = name_by_acc(a.name);
            db.insert(n.value, n, mvo
                ("name", n)
                ("key", pubkey_from_golos(a.memo_key))
            );
        }
        ilog("Done.");
    }

    void schedule_emit() {
        ilog("Scheduling emit...");
        db.start_section(config::system_account_name, N(gtransaction), "generated_transaction_object", 1);
        auto store_tx = [&](name code, name act_name, uint128_t sender_id, const bytes& data, const std::vector<std::pair<name,name>> &bwproviders = {}) {
            auto providebw = cyberway::chain::providebw(_info.golos.names.issuer, code);
            transaction tx{};
            tx.actions.emplace_back(action{{{code, config::active_name}}, code, act_name, data});
            for (const auto bwprovider: bwproviders) {
                auto providebw = cyberway::chain::providebw(bwprovider.first, bwprovider.second);
                tx.actions.emplace_back(action{{{providebw.provider, N(providebw)}},
                    providebw.get_account(), providebw.get_name(), fc::raw::pack(providebw)});
            }
            db.emplace<generated_transaction_object>(code, [&](auto& t){
                t.set(tx);
                t.trx_id = tx.id();
                t.sender = code;
                t.sender_id = sender_id;
                t.delay_until = _conf.initial_timestamp + fc::minutes(_info.golos.start_trx.delay_minutes);
                t.expiration = t.delay_until + fc::hours(_info.golos.start_trx.expiration_hours);
                t.published = _conf.initial_timestamp;
            });
        };
        store_tx(_info.golos.names.emission, N(emit), symbol(GLS).value(), {},
            {{_info.golos.names.issuer, _info.golos.names.emission},
             {_info.golos.names.issuer, _info.golos.names.control},
             {_info.golos.names.issuer, _info.golos.names.posting},
             {_info.golos.names.issuer, _info.golos.names.vesting}});

        db.start_section(_info.golos.names.emission, N(state), "state", 1);
        auto pk = N(state);
        auto start_time = (uint64_t)_visitor.gpo.time.sec_since_epoch() * 1000000;
        db.insert(pk, _info.golos.names.emission, mvo()
            ("id", pk)
            ("prev_emit", start_time)
            ("start_time", start_time)
            ("active", true));
        ilog("Done.");
    }

    void prepare_writer(const bfs::path& out_file, const genesis_ext_header &ext_hdr) {
        int n_sections = static_cast<int>(sys_contract_tables::_max) + _info.tables.size();
        if (_has_golos_state) {
            n_sections += static_cast<int>(contract_tables::_max);
        }
        db.start(out_file, n_sections, ext_hdr);
        db.prepare_serializers(_contracts);
    };

    void dump_closed_permlinks(const bfs::path& out_file) {
        ilog("Dumping permlinks of closed posts...");

        bfs::ofstream out;
        out.exceptions(std::ofstream::failbit | std::ofstream::badbit);
        out.open(out_file, std::ios_base::binary);

        for (const auto& cp : _visitor.closed_permlinks) {
            const auto& c = cp.second;
            std::string parent_permlink(name_by_idx(c.parent_author) == name() ? "" : _plnk_map[c.parent_permlink]);
            out
                << name_by_idx(c.author) << ';'
                << _plnk_map[c.permlink] << ';'
                << name_by_idx(c.parent_author) << ';'
                << parent_permlink << ';'
                << c.depth << ';'
                << c.children << '\n';
        }
        _visitor.closed_permlinks.clear();
        ilog("Done.");
    }

    std::vector<producer_key> get_producers() {
        if (_info.params.initial_prod_count == 0) {
            return {};
        }

        EOS_ASSERT(_info.params.initial_prod_count <= _visitor.witnesses.size(),
                genesis_exception, "initial_prod_count (${count}) too large. State has only ${witnesses} witnesses", 
                ("count", _info.params.initial_prod_count)("witnesses", _visitor.witnesses.size()));

        vector<const golos::witness_object*> witnesses;
        witnesses.reserve(_visitor.witnesses.size());
        for (const auto &witness: _visitor.witnesses) {
            witnesses.push_back(&witness);
        }

        std::sort(witnesses.begin(), witnesses.end(), [](const auto &lhs, const auto &rhs) {
            return (lhs->transit_to_cyberway_vote < rhs->transit_to_cyberway_vote) ||
                   (lhs->transit_to_cyberway_vote == rhs->transit_to_cyberway_vote && lhs->votes > rhs->votes);
        });

        std::vector<producer_key> producers;
        producers.reserve(_info.params.initial_prod_count);
        for (int i = 0; producers.size() < _info.params.initial_prod_count; ++i) {
            const auto &witness = *witnesses[i];
            account_name account = name_by_acc(witness.owner);
            public_key_type pubkey = pubkey_from_golos(witness.signing_key);
            if (pubkey != public_key_type() && witness.transit_to_cyberway_vote != (fc::time_point_sec(1476788400))) {
                ilog("Add producer ${witness} to initial schedule", ("witness", account));
                producers.push_back(producer_key{account, pubkey});
            }
        }
        return producers;
    }
};

genesis_create::genesis_create(): _impl(new genesis_create_impl()) {
}
genesis_create::~genesis_create() {
}

void genesis_create::read_state(const bfs::path& state_file, bool dump_closed_permlinks) {
    state_reader reader{state_file, _impl->_accs_map, _impl->_plnk_map};
    _impl->_visitor.dump_closed_permlinks = dump_closed_permlinks;
    reader.read_state(_impl->_visitor);
    _impl->_has_golos_state = true;
}


void genesis_create::dump_closed_permlinks(const bfs::path& out_file) {
    _impl->dump_closed_permlinks(out_file);
}

void genesis_create::write_genesis(
    const bfs::path& out_file, const genesis_info& info, const genesis_state& conf, const contracts_map& accs
) {
    _impl->_info = info;
    _impl->_conf = conf;
    _impl->_contracts = accs;

    if (_impl->_conf.initial_timestamp == time_point()) {
        _impl->_conf.initial_timestamp = _impl->_visitor.gpo.time + hours(1);
        ilog("Set initial_timestamp to ${initial_timestamp}", ("initial_timestamp", _impl->_conf.initial_timestamp));
    }

    genesis_ext_header ext_hdr;
    if (_impl->_has_golos_state) {
        ext_hdr.producers = _impl->get_producers();
    }

    _impl->prepare_writer(out_file, ext_hdr);
    _impl->store_contracts();
    _impl->store_auth_links();
    _impl->store_custom_tables();

    if (_impl->_has_golos_state) {
        _impl->store_accounts();
        _impl->store_bandwidth();
        _impl->store_posts();   // also pool and votes
        _impl->store_balances();
        _impl->store_stakes();
        _impl->store_delegation_records();
        _impl->store_withdrawals();
        _impl->store_witnesses();
        _impl->store_memo_keys();
        _impl->schedule_emit();
    }

    _impl->db.finalize();

    if (_impl->_has_golos_state) {
        _impl->_exp_info.conv_gbg = &_impl->_visitor.conv_gbg;
        _impl->_exp_info.conv_gls = &_impl->_visitor.conv_gls;
    }
}

const genesis_info& genesis_create::get_info() const {
    return _impl->_info;
}
const genesis_state& genesis_create::get_conf() const {
    return _impl->_conf;
}
const contracts_map& genesis_create::get_contracts() const {
    return _impl->_contracts;
}
const export_info& genesis_create::get_exp_info() const {
    return _impl->_exp_info;
}

name genesis_create::name_by_idx(acc_idx idx) const {
    return _impl->name_by_idx(idx);
}

supply_distributor genesis_create::get_gbg_to_golos_converter() const {
    return _impl->get_gbg_to_golos_converter();
}

string pubkey_string(const golos::public_key_type& k) {
    using checksummer = fc::crypto::checksummed_data<golos::public_key_type>;
    checksummer wrapper;
    wrapper.data = k;
    wrapper.check = checksummer::calculate_checksum(wrapper.data);
    auto packed = raw::pack(wrapper);
    auto tail = fc::to_base58(packed.data(), packed.size());
    return string(fc::crypto::config::public_key_legacy_prefix) + tail;
}

asset golos2sys(const asset& golos) {
    static const int64_t sys_precision = asset().get_symbol().precision();
    return asset(int_arithmetic::safe_prop(
        golos.get_amount(), sys_precision, static_cast<int64_t>(golos.get_symbol().precision())));
}

}} // cyberway::genesis
