#!/usr/bin/env bash
# Generates everything DDS Security needs, into security/generated/:
#   a private CA, one identity per role, and the CA-signed governance and
#   permissions documents. Keys stay local: they are never committed.
set -euo pipefail
cd "$(dirname "$0")"
OUT=generated
rm -rf "$OUT.tmp"
mkdir -p "$OUT.tmp"
cd "$OUT.tmp"

DAYS=3650
openssl ecparam -name prime256v1 -genkey -noout -out ca.key 2>/dev/null
openssl req -x509 -new -key ca.key -sha256 -days "$DAYS" -out ca.pem \
    -subj "/C=SG/O=CSD3116 Team A1/CN=Gingerbread CA" 2>/dev/null

make_identity() { # role, common name
    openssl ecparam -name prime256v1 -genkey -noout -out "$1.key" 2>/dev/null
    openssl req -new -key "$1.key" -out "$1.csr" \
        -subj "/C=SG/O=CSD3116 Team A1/CN=$2" 2>/dev/null
    openssl x509 -req -in "$1.csr" -CA ca.pem -CAkey ca.key -CAcreateserial \
        -days "$DAYS" -sha256 -out "$1.pem" 2>/dev/null
    rm -f "$1.csr"
}
make_identity server "Gingerbread Server"
make_identity client "Gingerbread Client"

subject_of() { openssl x509 -in "$1" -noout -subject -nameopt RFC2253 | sed 's/^subject=//'; }

# Governance: every part of the traffic is encrypted, and no participant
# without a CA-signed identity may join the domain at all.
cat > governance.xml <<'XML'
<?xml version="1.0" encoding="UTF-8"?>
<dds xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
     xsi:noNamespaceSchemaLocation="http://www.omg.org/spec/DDS-SECURITY/20170901/omg_shared_ca_governance.xsd">
  <domain_access_rules>
    <domain_rule>
      <domains><id_range><min>0</min><max>230</max></id_range></domains>
      <allow_unauthenticated_participants>false</allow_unauthenticated_participants>
      <enable_join_access_control>true</enable_join_access_control>
      <discovery_protection_kind>ENCRYPT</discovery_protection_kind>
      <liveliness_protection_kind>ENCRYPT</liveliness_protection_kind>
      <rtps_protection_kind>ENCRYPT</rtps_protection_kind>
      <topic_access_rules>
        <topic_rule>
          <topic_expression>*</topic_expression>
          <enable_discovery_protection>true</enable_discovery_protection>
          <enable_liveliness_protection>true</enable_liveliness_protection>
          <enable_read_access_control>true</enable_read_access_control>
          <enable_write_access_control>true</enable_write_access_control>
          <metadata_protection_kind>ENCRYPT</metadata_protection_kind>
          <data_protection_kind>ENCRYPT</data_protection_kind>
        </topic_rule>
      </topic_access_rules>
    </domain_rule>
  </domain_access_rules>
</dds>
XML

topic_list() { for t in "$@"; do printf '<topic>%s</topic>' "$t"; done; }
grant() { # name, subject, published topics, subscribed topics
    cat <<XML
    <grant name="$1">
      <subject_name>$2</subject_name>
      <validity><not_before>2020-01-01T00:00:00</not_before><not_after>2040-01-01T00:00:00</not_after></validity>
      <allow_rule>
        <domains><id_range><min>0</min><max>230</max></id_range></domains>
        <publish><topics>$3</topics></publish>
        <subscribe><topics>$4</topics></subscribe>
      </allow_rule>
      <default>DENY</default>
    </grant>
XML
}
# Permissions: a client certificate can never publish game state, positions or
# chat broadcasts, so even an authenticated client cannot impersonate the server.
TO_SERVER=$(topic_list AuthRequest JoinRequest Input GameAction ChatSend)
FROM_SERVER=$(topic_list AuthResponse JoinResponse Position GameState ChatMessage)
{
    cat <<'XML'
<?xml version="1.0" encoding="UTF-8"?>
<dds xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
     xsi:noNamespaceSchemaLocation="http://www.omg.org/spec/DDS-SECURITY/20170901/omg_shared_ca_permissions.xsd">
  <permissions>
XML
    grant ServerGrant "$(subject_of server.pem)" "$FROM_SERVER" "$TO_SERVER"
    grant ClientGrant "$(subject_of client.pem)" "$TO_SERVER" "$FROM_SERVER"
    printf '  </permissions>\n</dds>\n'
} > permissions.xml

for doc in governance permissions; do
    openssl smime -sign -text -in "$doc.xml" -signer ca.pem -inkey ca.key -out "$doc.p7s"
done
rm -f ca.srl
# The CA key is only needed to sign what is already signed above.
rm -f ca.key
chmod 600 ./*.key
cd ..
rm -rf "$OUT"
mv "$OUT.tmp" "$OUT"
echo "DDS Security identities generated in security/$OUT"
