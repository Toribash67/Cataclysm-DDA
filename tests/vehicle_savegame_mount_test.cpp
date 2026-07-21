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

    std::vector<point_rel_ms> before;
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
