"""Settings that must survive an upgrade: check them after it, then reset them.

See test/test_upgrade_settings_before.py.
"""

# pylint: disable=missing-function-docstring, line-too-long

from typing import Dict

from .test_upgrade_settings_before import TTL_48H, call


def test__the_settings_survived_the_upgrade(
    network: str, identity_non_controller: Dict[str, str]
) -> None:
    principal = identity_non_controller["principal"]
    roles = call(network, "getAdminRoles", "()")
    assert principal in roles and "AdminUpdate" in roles, roles
    assert "level = 0" in call(network, "get_access", "()")

    # the role works: the gateway (not a controller) reads the cleanup stats
    stats = call(
        network,
        "get_cache_cleanup_stats",
        "()",
        identity=identity_non_controller["identity"],
    )
    assert "Ok" in stats, stats
    assert "is_running = true" in stats, stats  # re-armed by canister_post_upgrade
    assert f"ttl_seconds = {TTL_48H:_}" in stats or f"ttl_seconds = {TTL_48H}" in stats, stats
    assert "max_decision_states = opt (99" in stats, stats
    assert "max_decision_states_per_scope = opt (50" in stats, stats


def test__reset_the_settings(
    network: str, identity_non_controller: Dict[str, str]
) -> None:
    principal = identity_non_controller["principal"]
    assert "Ok" in call(network, "revokeAdminRole", f'("{principal}")')
    assert "Ok" in call(network, "cache_cleanup_stop_timer", "()")
    response = call(
        network,
        "set_cache_cleanup_config",
        "(record { ttl_seconds = opt (21600 : nat64); max_decision_states = opt (256 : nat64); max_decision_states_per_scope = opt (8 : nat64) })",
    )
    assert "Ok" in response, response
