#include <sstream>
#include <string>
#include <vector>

#include "cata_catch.h"
#include "coordinates.h"
#include "flexbuffer_json.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "point.h"
#include "type_id.h"
#include "vehicle.h"

static const vproto_id vehicle_prototype_car( "car" );

// Serialize a live vehicle to a JSON string, then load it back into `dst`.
// `vehicle` has no default ctor and a deleted copy ctor, so `dst` is
// constructed by the caller with a null vproto_id (an empty vehicle, the same
// trick vehicles::finalize_prototypes uses) and filled by deserialize.
static void serialize_then_deserialize( const vehicle &src, vehicle &dst )
{
    std::ostringstream os;
    JsonOut jsout( os );
    src.serialize( jsout );

    JsonValue jv = json_loader::from_string( os.str() );
    dst.deserialize( jv.get_object() );
}

TEST_CASE( "vehicle_part_mount_round_trips_through_save", "[vehicle][savegame]" )
{
    clear_map();
    map &here = get_map();
    vehicle *veh_ptr = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                         0_degrees, -1, 0 );
    REQUIRE( veh_ptr != nullptr );
    veh_ptr->refresh();

    std::vector<tripoint_rel_ms> before;
    for( int i = 0; i < veh_ptr->part_count(); i++ ) {
        before.push_back( veh_ptr->part( i ).mount );
    }
    REQUIRE_FALSE( before.empty() );

    vehicle after{ vproto_id() };
    serialize_then_deserialize( *veh_ptr, after );
    REQUIRE( after.part_count() == veh_ptr->part_count() );
    for( int i = 0; i < after.part_count(); i++ ) {
        CHECK( after.part( i ).mount == before[i] );
    }
}

TEST_CASE( "vehicle_part_mount_z_round_trips_through_save", "[vehicle][savegame]" )
{
    clear_map();
    map &here = get_map();
    vehicle *veh_ptr = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                         0_degrees, -1, 0 );
    REQUIRE( veh_ptr != nullptr );
    veh_ptr->refresh();
    REQUIRE( veh_ptr->part_count() > 0 );

    // Directly set an upper-deck z on one part (JSON authoring arrives in
    // milestone 2; this exercises the serialization path only).
    veh_ptr->part( 0 ).mount.z() = 1;

    vehicle after{ vproto_id() };  // brace-init avoids most-vexing-parse
    serialize_then_deserialize( *veh_ptr, after );
    REQUIRE( after.part_count() == veh_ptr->part_count() );
    CHECK( after.part( 0 ).mount.z() == 1 );
    for( int i = 1; i < after.part_count(); i++ ) {
        CHECK( after.part( i ).mount.z() == 0 );
    }
}

TEST_CASE( "legacy_vehicle_part_without_mount_dz_defaults_to_z0", "[vehicle][savegame]" )
{
    clear_map();
    map &here = get_map();
    vehicle *veh_ptr = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                         0_degrees, -1, 0 );
    REQUIRE( veh_ptr != nullptr );
    veh_ptr->refresh();

    // A normal (all-z0) vehicle must serialize WITHOUT any mount_dz key, so old
    // saves and new saves are byte-compatible for ground-only vehicles.
    std::ostringstream os;
    JsonOut jsout( os );
    veh_ptr->serialize( jsout );
    CHECK( os.str().find( "mount_dz" ) == std::string::npos );

    // And loading such (legacy-format) JSON yields z == 0 everywhere.
    vehicle after{ vproto_id() };  // brace-init avoids most-vexing-parse
    serialize_then_deserialize( *veh_ptr, after );
    for( int i = 0; i < after.part_count(); i++ ) {
        CHECK( after.part( i ).mount.z() == 0 );
    }
}
