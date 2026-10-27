/* @ajax/mesh-abi-view-pipedemo's functions against vectors the Rust computed (alpha-rust-mesh, the same inputs). */
#include "mesh/abi/view_pipedemo.h"
#include "mesh/check.h"

#include <stdint.h>

int main(void) {
    { uint32_t m[16], want[16] = {0u, 0u, 0u, 0u, 3469435737u, 1421290948u, 3668113455u, 1619968666u, 3866791173u, 1818646384u, 4065468891u, 2017324102u, 4264146609u, 2216001820u, 167857031u, 2414679538u}; mesh_pipedemo_msg(0u, UINT64_C(0), 0u, m); for (int i = 0; i < 16; i++) CHECK_U(m[i], want[i]); }
    { uint32_t m[16] = {0u, 0u, 0u, 0u, 3469435737u, 1421290948u, 3668113455u, 1619968666u, 3866791173u, 1818646384u, 4065468891u, 2017324102u, 4264146609u, 2216001820u, 167857031u, 2414679538u}, d[8], want[8] = {709729990u, 1005096827u, 2931623650u, 2820212904u, 3233174510u, 3589445445u, 54259119u, 2347742402u}; mesh_pipedemo_sha256d_msg(m, d); for (int i = 0; i < 8; i++) CHECK_U(d[i], want[i]); }
    { uint32_t m[16] = {0u, 0u, 0u, 0u, 3469435737u, 1421290948u, 3668113455u, 1619968666u, 3866791173u, 1818646384u, 4065468891u, 2017324102u, 4264146609u, 2216001820u, 167857031u, 2414679538u}, g[12], want[12] = {0u, 0u, 0u, 0u, 709729990u, 1005096827u, 2931623650u, 2820212904u, 3233174510u, 3589445445u, 54259119u, 2347742402u}; mesh_pipedemo_digest(m, 0u, g); for (int i = 0; i < 12; i++) CHECK_U(g[i], want[i]); }
    { uint32_t m[16] = {0u, 0u, 0u, 0u, 3469435737u, 1421290948u, 3668113455u, 1619968666u, 3866791173u, 1818646384u, 4065468891u, 2017324102u, 4264146609u, 2216001820u, 167857031u, 2414679538u}, g[12], want[12] = {0u, 0u, 0u, 256u, 709729990u, 1005096827u, 2931623650u, 2820212904u, 3233174510u, 3589445445u, 54259119u, 2347742402u}; mesh_pipedemo_digest(m, 256u, g); for (int i = 0; i < 12; i++) CHECK_U(g[i], want[i]); }
    CHECK_U(mesh_pipedemo_payload(0u, 4), 3469435737u);
    CHECK_U(mesh_pipedemo_payload(0u, 5), 1421290948u);
    CHECK_U(mesh_pipedemo_payload(0u, 6), 3668113455u);
    CHECK_U(mesh_pipedemo_payload(0u, 7), 1619968666u);
    CHECK_U(mesh_pipedemo_payload(0u, 8), 3866791173u);
    CHECK_U(mesh_pipedemo_payload(0u, 9), 1818646384u);
    CHECK_U(mesh_pipedemo_payload(0u, 10), 4065468891u);
    CHECK_U(mesh_pipedemo_payload(0u, 11), 2017324102u);
    CHECK_U(mesh_pipedemo_payload(0u, 12), 4264146609u);
    CHECK_U(mesh_pipedemo_payload(0u, 13), 2216001820u);
    CHECK_U(mesh_pipedemo_payload(0u, 14), 167857031u);
    CHECK_U(mesh_pipedemo_payload(0u, 15), 2414679538u);
    { uint32_t m[16], want[16] = {7u, 2u, 1u, 1u, 4096063022u, 2047918233u, 4294740740u, 2246595951u, 198451162u, 2445273669u, 397128880u, 2643951387u, 595806598u, 2842629105u, 794484316u, 3041306823u}; mesh_pipedemo_msg(7u, UINT64_C(4294967298), 1u, m); for (int i = 0; i < 16; i++) CHECK_U(m[i], want[i]); }
    { uint32_t m[16] = {7u, 2u, 1u, 1u, 4096063022u, 2047918233u, 4294740740u, 2246595951u, 198451162u, 2445273669u, 397128880u, 2643951387u, 595806598u, 2842629105u, 794484316u, 3041306823u}, d[8], want[8] = {1227165531u, 1645736558u, 4080612562u, 1586557363u, 3710103006u, 2471307204u, 1147146001u, 3411586282u}; mesh_pipedemo_sha256d_msg(m, d); for (int i = 0; i < 8; i++) CHECK_U(d[i], want[i]); }
    { uint32_t m[16] = {7u, 2u, 1u, 1u, 4096063022u, 2047918233u, 4294740740u, 2246595951u, 198451162u, 2445273669u, 397128880u, 2643951387u, 595806598u, 2842629105u, 794484316u, 3041306823u}, g[12], want[12] = {7u, 2u, 1u, 1u, 1227165531u, 1645736558u, 4080612562u, 1586557363u, 3710103006u, 2471307204u, 1147146001u, 3411586282u}; mesh_pipedemo_digest(m, 0u, g); for (int i = 0; i < 12; i++) CHECK_U(g[i], want[i]); }
    { uint32_t m[16] = {7u, 2u, 1u, 1u, 4096063022u, 2047918233u, 4294740740u, 2246595951u, 198451162u, 2445273669u, 397128880u, 2643951387u, 595806598u, 2842629105u, 794484316u, 3041306823u}, g[12], want[12] = {7u, 2u, 1u, 257u, 1227165531u, 1645736558u, 4080612562u, 1586557363u, 3710103006u, 2471307204u, 1147146001u, 3411586282u}; mesh_pipedemo_digest(m, 256u, g); for (int i = 0; i < 12; i++) CHECK_U(g[i], want[i]); }
    CHECK_U(mesh_pipedemo_payload(7u, 4), 4096063022u);
    CHECK_U(mesh_pipedemo_payload(7u, 5), 2047918233u);
    CHECK_U(mesh_pipedemo_payload(7u, 6), 4294740740u);
    CHECK_U(mesh_pipedemo_payload(7u, 7), 2246595951u);
    CHECK_U(mesh_pipedemo_payload(7u, 8), 198451162u);
    CHECK_U(mesh_pipedemo_payload(7u, 9), 2445273669u);
    CHECK_U(mesh_pipedemo_payload(7u, 10), 397128880u);
    CHECK_U(mesh_pipedemo_payload(7u, 11), 2643951387u);
    CHECK_U(mesh_pipedemo_payload(7u, 12), 595806598u);
    CHECK_U(mesh_pipedemo_payload(7u, 13), 2842629105u);
    CHECK_U(mesh_pipedemo_payload(7u, 14), 794484316u);
    CHECK_U(mesh_pipedemo_payload(7u, 15), 3041306823u);
    { uint32_t m[16], want[16] = {4294967295u, 4294967295u, 4294967295u, 2u, 3260773958u, 1212629169u, 3459451676u, 1411306887u, 3658129394u, 1609984605u, 3856807112u, 1808662323u, 4055484830u, 2007340041u, 4254162548u, 2206017759u}; mesh_pipedemo_msg(4294967295u, UINT64_C(18446744073709551615), 2u, m); for (int i = 0; i < 16; i++) CHECK_U(m[i], want[i]); }
    { uint32_t m[16] = {4294967295u, 4294967295u, 4294967295u, 2u, 3260773958u, 1212629169u, 3459451676u, 1411306887u, 3658129394u, 1609984605u, 3856807112u, 1808662323u, 4055484830u, 2007340041u, 4254162548u, 2206017759u}, d[8], want[8] = {2200466271u, 3664171646u, 1299924718u, 1761254264u, 1210684333u, 1227006414u, 4154316208u, 987238893u}; mesh_pipedemo_sha256d_msg(m, d); for (int i = 0; i < 8; i++) CHECK_U(d[i], want[i]); }
    { uint32_t m[16] = {4294967295u, 4294967295u, 4294967295u, 2u, 3260773958u, 1212629169u, 3459451676u, 1411306887u, 3658129394u, 1609984605u, 3856807112u, 1808662323u, 4055484830u, 2007340041u, 4254162548u, 2206017759u}, g[12], want[12] = {4294967295u, 4294967295u, 4294967295u, 2u, 2200466271u, 3664171646u, 1299924718u, 1761254264u, 1210684333u, 1227006414u, 4154316208u, 987238893u}; mesh_pipedemo_digest(m, 0u, g); for (int i = 0; i < 12; i++) CHECK_U(g[i], want[i]); }
    { uint32_t m[16] = {4294967295u, 4294967295u, 4294967295u, 2u, 3260773958u, 1212629169u, 3459451676u, 1411306887u, 3658129394u, 1609984605u, 3856807112u, 1808662323u, 4055484830u, 2007340041u, 4254162548u, 2206017759u}, g[12], want[12] = {4294967295u, 4294967295u, 4294967295u, 258u, 2200466271u, 3664171646u, 1299924718u, 1761254264u, 1210684333u, 1227006414u, 4154316208u, 987238893u}; mesh_pipedemo_digest(m, 256u, g); for (int i = 0; i < 12; i++) CHECK_U(g[i], want[i]); }
    CHECK_U(mesh_pipedemo_payload(4294967295u, 4), 3260773958u);
    CHECK_U(mesh_pipedemo_payload(4294967295u, 5), 1212629169u);
    CHECK_U(mesh_pipedemo_payload(4294967295u, 6), 3459451676u);
    CHECK_U(mesh_pipedemo_payload(4294967295u, 7), 1411306887u);
    CHECK_U(mesh_pipedemo_payload(4294967295u, 8), 3658129394u);
    CHECK_U(mesh_pipedemo_payload(4294967295u, 9), 1609984605u);
    CHECK_U(mesh_pipedemo_payload(4294967295u, 10), 3856807112u);
    CHECK_U(mesh_pipedemo_payload(4294967295u, 11), 1808662323u);
    CHECK_U(mesh_pipedemo_payload(4294967295u, 12), 4055484830u);
    CHECK_U(mesh_pipedemo_payload(4294967295u, 13), 2007340041u);
    CHECK_U(mesh_pipedemo_payload(4294967295u, 14), 4254162548u);
    CHECK_U(mesh_pipedemo_payload(4294967295u, 15), 2206017759u);
    { uint32_t m[16], want[16] = {123456u, 4106810228u, 229u, 3u, 3345114009u, 1296969220u, 3543791727u, 1495646938u, 3742469445u, 1694324656u, 3941147163u, 1893002374u, 4139824881u, 2091680092u, 43535303u, 2290357810u}; mesh_pipedemo_msg(123456u, UINT64_C(987654321012), 3u, m); for (int i = 0; i < 16; i++) CHECK_U(m[i], want[i]); }
    { uint32_t m[16] = {123456u, 4106810228u, 229u, 3u, 3345114009u, 1296969220u, 3543791727u, 1495646938u, 3742469445u, 1694324656u, 3941147163u, 1893002374u, 4139824881u, 2091680092u, 43535303u, 2290357810u}, d[8], want[8] = {3887679330u, 3977116818u, 1863808533u, 863933500u, 1670136128u, 1741747026u, 959475053u, 1052517006u}; mesh_pipedemo_sha256d_msg(m, d); for (int i = 0; i < 8; i++) CHECK_U(d[i], want[i]); }
    { uint32_t m[16] = {123456u, 4106810228u, 229u, 3u, 3345114009u, 1296969220u, 3543791727u, 1495646938u, 3742469445u, 1694324656u, 3941147163u, 1893002374u, 4139824881u, 2091680092u, 43535303u, 2290357810u}, g[12], want[12] = {123456u, 4106810228u, 229u, 3u, 3887679330u, 3977116818u, 1863808533u, 863933500u, 1670136128u, 1741747026u, 959475053u, 1052517006u}; mesh_pipedemo_digest(m, 0u, g); for (int i = 0; i < 12; i++) CHECK_U(g[i], want[i]); }
    { uint32_t m[16] = {123456u, 4106810228u, 229u, 3u, 3345114009u, 1296969220u, 3543791727u, 1495646938u, 3742469445u, 1694324656u, 3941147163u, 1893002374u, 4139824881u, 2091680092u, 43535303u, 2290357810u}, g[12], want[12] = {123456u, 4106810228u, 229u, 259u, 3887679330u, 3977116818u, 1863808533u, 863933500u, 1670136128u, 1741747026u, 959475053u, 1052517006u}; mesh_pipedemo_digest(m, 256u, g); for (int i = 0; i < 12; i++) CHECK_U(g[i], want[i]); }
    CHECK_U(mesh_pipedemo_payload(123456u, 4), 3345114009u);
    CHECK_U(mesh_pipedemo_payload(123456u, 5), 1296969220u);
    CHECK_U(mesh_pipedemo_payload(123456u, 6), 3543791727u);
    CHECK_U(mesh_pipedemo_payload(123456u, 7), 1495646938u);
    CHECK_U(mesh_pipedemo_payload(123456u, 8), 3742469445u);
    CHECK_U(mesh_pipedemo_payload(123456u, 9), 1694324656u);
    CHECK_U(mesh_pipedemo_payload(123456u, 10), 3941147163u);
    CHECK_U(mesh_pipedemo_payload(123456u, 11), 1893002374u);
    CHECK_U(mesh_pipedemo_payload(123456u, 12), 4139824881u);
    CHECK_U(mesh_pipedemo_payload(123456u, 13), 2091680092u);
    CHECK_U(mesh_pipedemo_payload(123456u, 14), 43535303u);
    CHECK_U(mesh_pipedemo_payload(123456u, 15), 2290357810u);
    return mesh_check_done();
}
