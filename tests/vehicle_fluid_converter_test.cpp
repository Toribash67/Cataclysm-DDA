#include <optional>

#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "point.h"
#include "type_id.h"
#include "units.h"
#include "vehicle.h"

static const efftype_id effect_blind( "blind" );

static const itype_id fuel_type_battery( "battery" );
static const itype_id itype_ammonia_liquid( "ammonia_liquid" );
static const itype_id itype_water_clean( "water_clean" );

static const vpart_id vpart_ammonia_synthesizer( "ammonia_synthesizer" );
static const vpart_id vpart_frame( "frame" );
static const vpart_id vpart_large_storage_battery( "large_storage_battery" );
static const vpart_id vpart_tank_small( "tank_small" );

static const vproto_id vehicle_prototype_none( "none" );

static void reset_player_safe()
{
    map &here = get_map();
    Character &player_character = get_player_character();
    REQUIRE( !player_character.in_vehicle );
    player_character.setpos( here, tripoint_bub_ms::zero );
    player_character.add_effect( effect_blind, 1_turns, true );
}

// Build a small immobile "ammonia plant": water tank + output tank + large
// battery + the synthesizer, each on its own contiguous frame tile.
static vehicle *build_synth_rig( map &here, int &water_idx, int &ammonia_idx, int &synth_idx )
{
    clear_vehicles();
    reset_player_safe();
    build_test_map( ter_id( "t_pavement" ) );

    vehicle *veh = here.add_vehicle( vehicle_prototype_none, tripoint_bub_ms{ 5, 5, 0 }, 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    const auto add_part = [&]( const point_rel_ms & mount, const vpart_id & vpid ) {
        REQUIRE( veh->install_part( here, mount, vpart_frame ) != -1 );
        const int idx = veh->install_part( here, mount, vpid );
        REQUIRE( idx != -1 );
        return idx;
    };

    water_idx = add_part( point_rel_ms::zero, vpart_tank_small );
    ammonia_idx = add_part( point_rel_ms( 1, 0 ), vpart_tank_small );
    add_part( point_rel_ms( 2, 0 ), vpart_large_storage_battery );
    synth_idx = add_part( point_rel_ms( 3, 0 ), vpart_ammonia_synthesizer );

    veh->refresh();
    here.add_vehicle_to_cache( veh );
    return veh;
}

TEST_CASE( "fluid_converter converts water to ammonia when powered", "[vehicle][power][fluid_converter]" )
{
    map &here = get_map();
    int water_idx = -1;
    int ammonia_idx = -1;
    int synth_idx = -1;
    vehicle *veh = build_synth_rig( here, water_idx, ammonia_idx, synth_idx );

    veh->part( water_idx ).ammo_set( itype_water_clean, 40 );
    veh->part( ammonia_idx ).ammo_unset();
    veh->charge_battery( here, 500000 );

    const int water_before = veh->part( water_idx ).ammo_remaining();
    const int batt_before = static_cast<int>( veh->fuel_left( here, fuel_type_battery ) );
    REQUIRE( water_before == 40 );
    REQUIRE( batt_before > 0 );

    calendar::turn = calendar::turn_zero + 2_days;
    veh->update_time( here, calendar::turn );                 // prime last_update
    veh->update_time( here, calendar::turn + 1_hours );       // elapse 1 hour

    const int water_after = veh->part( water_idx ).ammo_remaining();
    const int ammonia_after = veh->part( ammonia_idx ).ammo_remaining();

    CHECK( water_after < water_before );
    CHECK( ammonia_after > 0 );
    CHECK( veh->part( ammonia_idx ).ammo_current() == itype_ammonia_liquid );
    CHECK( ( water_before - water_after ) == ammonia_after );     // 1:1 conversion
    CHECK( static_cast<int>( veh->fuel_left( here, fuel_type_battery ) ) < batt_before );
}

TEST_CASE( "fluid_converter makes nothing without power", "[vehicle][power][fluid_converter]" )
{
    map &here = get_map();
    int water_idx = -1;
    int ammonia_idx = -1;
    int synth_idx = -1;
    vehicle *veh = build_synth_rig( here, water_idx, ammonia_idx, synth_idx );

    veh->part( water_idx ).ammo_set( itype_water_clean, 40 );
    veh->part( ammonia_idx ).ammo_unset();
    // battery left empty
    REQUIRE( veh->fuel_left( here, fuel_type_battery ) == 0 );

    calendar::turn = calendar::turn_zero + 2_days;
    veh->update_time( here, calendar::turn );
    veh->update_time( here, calendar::turn + 1_hours );

    CHECK( veh->part( ammonia_idx ).ammo_remaining() == 0 );
    CHECK( veh->part( water_idx ).ammo_remaining() == 40 );
}

TEST_CASE( "fluid_converter makes nothing when toggled off", "[vehicle][power][fluid_converter]" )
{
    map &here = get_map();
    int water_idx = -1;
    int ammonia_idx = -1;
    int synth_idx = -1;
    vehicle *veh = build_synth_rig( here, water_idx, ammonia_idx, synth_idx );

    veh->part( water_idx ).ammo_set( itype_water_clean, 40 );
    veh->part( ammonia_idx ).ammo_unset();
    veh->charge_battery( here, 500000 );
    veh->part( synth_idx ).enabled = false;

    calendar::turn = calendar::turn_zero + 2_days;
    veh->update_time( here, calendar::turn );
    veh->update_time( here, calendar::turn + 1_hours );

    CHECK( veh->part( ammonia_idx ).ammo_remaining() == 0 );
    CHECK( veh->part( water_idx ).ammo_remaining() == 40 );
}
