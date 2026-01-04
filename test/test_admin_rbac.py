"""Test Admin RBAC endpoints

First deploy the canister:
$ dfx start --clean --background
$ dfx deploy --network local

Then run the tests:
$ pytest -vv --network local test/test_admin_rbac.py

Or run a specific test:
$ pytest -vv --network local test/test_admin_rbac.py::test__getAdminRoles_anonymous

"""
# pylint: disable=missing-function-docstring, unused-import, wildcard-import, unused-wildcard-import, line-too-long

from pathlib import Path
from typing import Dict
import pytest
from icpp.smoketest import call_canister_api, dict_to_candid_text

# Path to the dfx.json file
DFX_JSON_PATH = Path(__file__).parent / "../dfx.json"

# Canister in the dfx.json file we want to test
CANISTER_NAME = "llama_cpp"


# =============================================================================
# Admin RBAC Endpoints - Anonymous Access Denial Tests
# =============================================================================

def test__getAdminRoles_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    """Test getAdminRoles rejects anonymous caller"""
    assert identity_anonymous["identity"] == "anonymous"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="getAdminRoles",
        canister_argument="()",
        network=network,
    )
    expected_response = '(variant { Err = variant { Other = "Access Denied" } })'
    assert response == expected_response


def test__assignAdminRole_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    """Test assignAdminRole rejects anonymous caller"""
    assert identity_anonymous["identity"] == "anonymous"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="assignAdminRole",
        canister_argument='(record { "principal" = "aaaaa-aa"; role = variant { AdminQuery }; note = "test" })',
        network=network,
    )
    expected_response = '(variant { Err = variant { Other = "Access Denied" } })'
    assert response == expected_response


def test__revokeAdminRole_anonymous(identity_anonymous: Dict[str, str], network: str) -> None:
    """Test revokeAdminRole rejects anonymous caller"""
    assert identity_anonymous["identity"] == "anonymous"

    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="revokeAdminRole",
        canister_argument='("aaaaa-aa")',
        network=network,
    )
    expected_response = '(variant { Err = variant { Other = "Access Denied" } })'
    assert response == expected_response


# =============================================================================
# Admin RBAC Management - Success Tests (controller can manage roles)
# =============================================================================

def test__setup_cleanup_admin_roles(network: str) -> None:
    """Setup: Clean up any existing admin roles from previous test runs"""
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="revokeAdminRole",
        canister_argument='("aaaaa-aa")',
        network=network,
    )
    # Accept either Ok (role revoked) or Err (role not found)
    assert response in [
        '(variant { Ok = "Admin role revoked for aaaaa-aa" })',
        '(variant { Err = variant { Other = "Principal not found" } })'
    ]


def test__getAdminRoles_empty(network: str) -> None:
    """Test getAdminRoles returns empty list initially"""
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="getAdminRoles",
        canister_argument="()",
        network=network,
    )
    expected_response = '(variant { Ok = vec {} })'
    assert response == expected_response


def test__assignAdminRole_AdminQuery(network: str) -> None:
    """Test assignAdminRole assigns AdminQuery role"""
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="assignAdminRole",
        canister_argument='(record { "principal" = "aaaaa-aa"; role = variant { AdminQuery }; note = "Test admin query role" })',
        network=network,
    )
    assert response.startswith('(variant { Ok = record {')
    assert '"principal" = "aaaaa-aa"' in response or 'principal = "aaaaa-aa"' in response
    # Verify role is a proper variant, not text
    assert 'role = variant { AdminQuery }' in response


def test__getAdminRoles_after_assign(network: str) -> None:
    """Test getAdminRoles returns assigned roles with proper variant format"""
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="getAdminRoles",
        canister_argument="()",
        network=network,
    )
    assert response.startswith('(variant { Ok = vec {')
    assert 'aaaaa-aa' in response
    # Verify role is a proper variant, not text
    assert 'role = variant { AdminQuery }' in response
    # Verify other fields are present
    assert 'assignedBy =' in response
    assert 'assignedAt =' in response
    assert 'note = "Test admin query role"' in response


def test__assignAdminRole_AdminUpdate(network: str) -> None:
    """Test assignAdminRole assigns AdminUpdate role (upsert overwrites AdminQuery)"""
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="assignAdminRole",
        canister_argument='(record { "principal" = "aaaaa-aa"; role = variant { AdminUpdate }; note = "Upgraded to admin update" })',
        network=network,
    )
    assert response.startswith('(variant { Ok = record {')
    # Verify role is a proper variant, not text
    assert 'role = variant { AdminUpdate }' in response


def test__getAdminRoles_after_update(network: str) -> None:
    """Test getAdminRoles returns AdminUpdate role with proper variant format"""
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="getAdminRoles",
        canister_argument="()",
        network=network,
    )
    assert response.startswith('(variant { Ok = vec {')
    assert 'aaaaa-aa' in response
    # Verify role is a proper variant with AdminUpdate (was upgraded from AdminQuery)
    assert 'role = variant { AdminUpdate }' in response
    assert 'note = "Upgraded to admin update"' in response


# =============================================================================
# Multiple Principals Tests - verify CandidTypeVecVariant with multiple entries
# =============================================================================

def test__assignAdminRole_second_principal(network: str) -> None:
    """Test assigning AdminQuery role to a second principal"""
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="assignAdminRole",
        canister_argument='(record { "principal" = "rrkah-fqaaa-aaaaa-aaaaq-cai"; role = variant { AdminQuery }; note = "Second admin" })',
        network=network,
    )
    assert response.startswith('(variant { Ok = record {')
    assert 'role = variant { AdminQuery }' in response


def test__getAdminRoles_multiple_principals(network: str) -> None:
    """Test getAdminRoles returns multiple principals with different roles"""
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="getAdminRoles",
        canister_argument="()",
        network=network,
    )
    assert response.startswith('(variant { Ok = vec {')
    # Verify both principals are present
    assert 'aaaaa-aa' in response
    assert 'rrkah-fqaaa-aaaaa-aaaaq-cai' in response
    # Verify both role variants are present (AdminUpdate for first, AdminQuery for second)
    assert 'role = variant { AdminUpdate }' in response
    assert 'role = variant { AdminQuery }' in response
    # Verify both notes are present
    assert 'note = "Upgraded to admin update"' in response
    assert 'note = "Second admin"' in response


def test__revokeAdminRole_second_principal(network: str) -> None:
    """Clean up: revoke second principal's role"""
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="revokeAdminRole",
        canister_argument='("rrkah-fqaaa-aaaaa-aaaaq-cai")',
        network=network,
    )
    expected_response = '(variant { Ok = "Admin role revoked for rrkah-fqaaa-aaaaa-aaaaq-cai" })'
    assert response == expected_response


def test__revokeAdminRole(network: str) -> None:
    """Test revokeAdminRole removes role"""
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="revokeAdminRole",
        canister_argument='("aaaaa-aa")',
        network=network,
    )
    expected_response = '(variant { Ok = "Admin role revoked for aaaaa-aa" })'
    assert response == expected_response


def test__revokeAdminRole_not_found(network: str) -> None:
    """Test revokeAdminRole returns error for non-existent principal"""
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="revokeAdminRole",
        canister_argument='("non-existent-principal")',
        network=network,
    )
    expected_response = '(variant { Err = variant { Other = "Principal not found" } })'
    assert response == expected_response


def test__getAdminRoles_after_revoke(network: str) -> None:
    """Test getAdminRoles returns empty after revoke"""
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="getAdminRoles",
        canister_argument="()",
        network=network,
    )
    expected_response = '(variant { Ok = vec {} })'
    assert response == expected_response
