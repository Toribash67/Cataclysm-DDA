#include <sstream>

#include "avatar.h"
#include "cata_catch.h"
#include "clzones.h"
#include "creature_tracker.h"
#include "damage.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "monster.h"
#include "mtype.h"
#include "player_helpers.h"
#include "vehicle.h"
#include "veh_type.h"
#include "type_id.h"
#include "coordinates.h"

static const vproto_id vehicle_prototype_car( "car" );
static const damage_type_id damage_bash( "bash" );
static const mtype_id mon_zombie( "mon_zombie" );
// your_fac is declared in clzones.h (included above)

TEST_CASE( "vehicle_prototype_parts_default_to_z_zero", "[vehicle][multifloor]" )
{
    // Every shipped vehicle omits "z", so all authored parts must land on z == 0.
    const vehicle_prototype &proto = vehicle_prototype_car.obj();
    REQUIRE( !proto.parts.empty() );
    for( const vehicle_prototype::part_def &pt : proto.parts ) {
        CHECK( pt.pos.z() == 0 );
    }
}

TEST_CASE( "vehicle_install_part_accepts_tripoint_mount", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                     0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    // The 3D overload with z == 0 must behave exactly like the 2D one.
    const tripoint_rel_ms mount( 0, 0, 0 );
    CHECK( !veh->parts_at_relative( mount, false, false ).empty() );
}

static const vpart_id vpart_ladder_internal( "ladder_internal" );

TEST_CASE( "vertical_traversal_flag_is_recognized", "[vehicle][multifloor]" )
{
    // The flag must resolve through the fast-path enum, not just the string set.
    CHECK( vpart_ladder_internal.obj().has_flag( VPFLAG_VERTICAL_TRAVERSAL ) );
}

static const vpart_id vpart_frame( "frame" );

TEST_CASE( "upper_deck_frame_supported_by_frame_below", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                     0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    const vpart_info &hdframe = vpart_id( "hdframe" ).obj();

    // (-1, -2) is planar-adjacent to the car's real structure at (-1, -1) but is
    // itself bare terrain -- unlike (0, 0), which the "car" prototype's own footprint
    // already occupies with a structural frame. Before anything is installed on the
    // ground tile below, nothing supports an upper-deck frame at (-1, -2, 1).
    const tripoint_rel_ms above( -1, -2, 1 );
    CHECK( !veh->can_mount( above, hdframe ).success() );

    // A plain structural frame on that new ground tile. A frame directly below now
    // legalizes an upper-deck frame stacked on it -- no ladder/traversal part required.
    const tripoint_rel_ms ground( -1, -2, 0 );
    REQUIRE( veh->install_part( here, ground, vpart_frame ) >= 0 );

    CHECK( veh->can_mount( above, hdframe ).success() );
}

TEST_CASE( "upper_deck_nonframe_part_needs_no_frame_directly_below", "[vehicle][multifloor]" )
{
    // A non-frame upper-deck part (seat) mounts by planar adjacency to an existing
    // upper-deck frame, with NO frame directly beneath the seat itself -- the vertical
    // analog of the in-plane / external-attach rule. (Setup itself depends on the
    // frame-below mount rule: installing the upper frame requires a frame below it.)
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                     0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    const tripoint_rel_ms ground( 2, -2, 0 );
    REQUIRE( veh->install_part( here, ground, vpart_frame ) >= 0 );
    const tripoint_rel_ms upper_frame( 2, -2, 1 );
    REQUIRE( veh->install_part( here, upper_frame, vpart_frame ) >= 0 );

    // A frame one tile further out on the upper deck: planar-adjacent to upper_frame,
    // but the tile directly beneath it (3,-2,0) is empty, so it is legalized purely by
    // in-plane adjacency (supported_in_plane), not by anything below it.
    const tripoint_rel_ms outer_frame_tile( 3, -2, 1 );
    REQUIRE( veh->parts_at_relative( tripoint_rel_ms( 3, -2, 0 ), false ).empty() );
    REQUIRE( veh->install_part( here, outer_frame_tile, vpart_frame ) >= 0 );

    // A non-frame part (seat) can then attach on that same tile. It needs no frame-below
    // check of its own -- can_mount's "first part on a tile must be structural" rule
    // already guarantees a structural frame occupies the tile before any non-frame part
    // can join it, so the seat rides on the frame's support, not on anything beneath it.
    CHECK( veh->can_mount( outer_frame_tile, vpart_id( "seat" ).obj() ).success() );
}

static const vproto_id vehicle_prototype_test_bus_2floor( "test_bus_2floor" );

TEST_CASE( "two_floor_bus_prototype_has_upper_deck_parts", "[vehicle][multifloor]" )
{
    const vehicle_prototype &proto = vehicle_prototype_test_bus_2floor.obj();
    int upper = 0;
    for( const vehicle_prototype::part_def &pt : proto.parts ) {
        if( pt.pos.z() == 1 ) {
            upper++;
        }
    }
    CHECK( upper > 0 );
}

TEST_CASE( "two_floor_bus_spawns_with_parts_on_both_decks", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    CHECK( !veh->parts_at_relative( tripoint_rel_ms( 0, 0, 0 ), false, false ).empty() );
    CHECK( !veh->parts_at_relative( tripoint_rel_ms( 0, 0, 1 ), false, false ).empty() );
}

// M3 §6.3 composition matrix (mount side): after precalc_mounts, every part's
// precalc.z must equal its mount.z, across all 4 cardinal rotations and both
// decks (with no ramp displacement, precalc_z_delta == 0). This is the NEW logic
// M3 introduces; the ramp side (mount.z=0 x ramp) is covered by vehicle_ramp_test,
// and the cross term (mount.z=1 x ramp) lands with the driving test in M5.
TEST_CASE( "precalc_z_composes_from_mount_z_across_rotations", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    // Prove the bus actually has an upper deck, else the test is vacuous.
    bool saw_upper = false;
    for( const vpart_reference &vpr : veh->get_all_parts() ) {
        if( !vpr.part().removed && vpr.part().mount.z() == 1 ) {
            saw_upper = true;
            break;
        }
    }
    REQUIRE( saw_upper );

    for( const units::angle rot : {
             0_degrees, 90_degrees, 180_degrees, 270_degrees
         } ) {
        veh->precalc_mounts( 0, rot, point_rel_ms::zero );
        for( const vpart_reference &vpr : veh->get_all_parts() ) {
            const vehicle_part &vp = vpr.part();
            if( vp.removed ) {
                continue;
            }
            INFO( "rotation " << to_degrees( rot ) << " mount z " << vp.mount.z() );
            CHECK( vp.precalc[0].z() == vp.mount.z() );
        }
    }
}

TEST_CASE( "refresh_bounding_box_tracks_upper_deck_z", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    veh->refresh();
    CHECK( veh->mount_min_z() == 0 );
    CHECK( veh->mount_max_z() == 1 );
}

TEST_CASE( "single_floor_vehicle_bounding_box_z_is_zero", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    veh->refresh();
    // No-op guarantee: every shipped vehicle stays a single ground deck.
    CHECK( veh->mount_min_z() == 0 );
    CHECK( veh->mount_max_z() == 0 );
}

TEST_CASE( "two_floor_bus_upper_deck_can_carry_cargo", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    const int cargo = veh->part_with_feature( tripoint_rel_ms( 1, 0, 1 ), "CARGO", false );
    CHECK( cargo >= 0 );
}

// Returns the index of the structure part at the given relative mount, or -1.
static int structure_part_at( const vehicle &veh, const tripoint_rel_ms &mount )
{
    for( const int idx : veh.parts_at_relative( mount, false ) ) {
        if( veh.part( idx ).info().location == "structure" ) {
            return idx;
        }
    }
    return -1;
}

// A single stacked frame is the sole structural bridge to a second upper-deck tile.
// Removing the bridge frame would strand that tile, so can_unmount must refuse it --
// the 3D, frame-gated "would this split the vehicle?" check. A traversal-gated (pre-
// change) connected_neighbours gives the bridge frame only its planar neighbour, sees
// a removable end tile, and wrongly allows the removal.
TEST_CASE( "upper_deck_removal_blocked_when_only_link_is_frame_bridge", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                     0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    // b0: ground bridge frame, planar-adjacent to the car's real structure.
    const tripoint_rel_ms b0( 2, -2, 0 );
    REQUIRE( veh->install_part( here, b0, vpart_frame ) >= 0 );

    // u0: upper frame stacked on b0. Its 3D neighbours are b0 (down, through the frame)
    // and u1 (planar, same z).
    const tripoint_rel_ms u0( 2, -2, 1 );
    REQUIRE( veh->install_part( here, u0, vpart_frame ) >= 0 );

    // u1: a second upper-deck tile reachable from the rest of the vehicle ONLY through
    // u0 -- there is no frame under u1 at (3,-2,0).
    const tripoint_rel_ms u1( 3, -2, 1 );
    REQUIRE( veh->install_part( here, u1, vpart_frame ) >= 0 );

    // (3,-2,0) must be bare terrain for u1's "no frame below" premise to hold.
    REQUIRE( veh->parts_at_relative( tripoint_rel_ms( 3, -2, 0 ), false ).empty() );

    const int idx_u0 = structure_part_at( *veh, u0 );
    REQUIRE( idx_u0 >= 0 );

    CHECK( !veh->can_unmount( veh->part( idx_u0 ), false ).success() );
}

TEST_CASE( "removing_ladder_keeps_decks_connected", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    const int ladder = veh->part_with_feature( tripoint_rel_ms( 0, 0, 0 ),
                       "VERTICAL_TRAVERSAL", false );
    REQUIRE( ladder >= 0 );

    // Removing the climb affordance must NOT split the vehicle: the decks are held
    // together structurally by the stacked frames, independent of traversal.
    veh->remove_part( veh->part( ladder ) );
    veh->find_and_split_vehicles( here, {} );

    bool still_has_upper = false;
    for( const vpart_reference &vpr : veh->get_all_parts() ) {
        if( vpr.part().mount.z() == 1 && !vpr.part().removed ) {
            still_has_upper = true;
            break;
        }
    }
    CHECK( still_has_upper );
}

TEST_CASE( "part_displayed_at_resolves_per_deck", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    // Ground deck at (0,0,0) shows a ground structure part; upper deck at
    // (0,0,1) shows the deck floor. They must resolve to DIFFERENT parts.
    const int ground = veh->part_displayed_at( tripoint_rel_ms( 0, 0, 0 ), false, true, false );
    const int upper  = veh->part_displayed_at( tripoint_rel_ms( 0, 0, 1 ), false, true, false );
    REQUIRE( ground >= 0 );
    REQUIRE( upper >= 0 );
    CHECK( ground != upper );
    CHECK( veh->part( upper ).mount.z() == 1 );
}

TEST_CASE( "allows_deck_traversal_up_from_connector", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    // Standing on the connector at (0,0,0), climbing up to the boardable deck floor at (0,0,1).
    CHECK( veh->allows_deck_traversal( tripoint_rel_ms( 0, 0, 0 ), 1 ) );
    // From the upper landing (0,0,1), climbing back down to (0,0,0): connector is on the tile below.
    CHECK( veh->allows_deck_traversal( tripoint_rel_ms( 0, 0, 1 ), -1 ) );
}

TEST_CASE( "allows_deck_traversal_rejects_non_connector_and_bad_dz", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    // (0,1,0) is a plain seat tile with no connector: no vertical travel either way.
    CHECK_FALSE( veh->allows_deck_traversal( tripoint_rel_ms( 0, 1, 0 ), 1 ) );
    CHECK_FALSE( veh->allows_deck_traversal( tripoint_rel_ms( 0, 1, 1 ), -1 ) );
    // dz other than +/-1 is never a deck move (defends the early branch in vertical_move).
    CHECK_FALSE( veh->allows_deck_traversal( tripoint_rel_ms( 0, 0, 0 ), 2 ) );
    CHECK_FALSE( veh->allows_deck_traversal( tripoint_rel_ms( 0, 0, 0 ), 0 ) );
}

TEST_CASE( "allows_deck_traversal_requires_floor_at_destination", "[vehicle][multifloor]" )
{
    // A lone connector with nothing built above must not let you climb into empty air.
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                     0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    // Extend the car by a ground tile carrying a connector, but build nothing on z=1 above it.
    const tripoint_rel_ms ground( 2, -2, 0 );
    REQUIRE( veh->install_part( here, ground, vpart_frame ) >= 0 );
    REQUIRE( veh->install_part( here, ground, vpart_ladder_internal ) >= 0 );
    // Connector present, but (2,-2,1) has no boardable part -> no traversal.
    CHECK_FALSE( veh->allows_deck_traversal( ground, 1 ) );
}

TEST_CASE( "try_vehicle_deck_move_climbs_between_decks", "[vehicle][multifloor]" )
{
    clear_map();
    clear_avatar();
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    avatar &u = get_avatar();
    // Stand the avatar on the ground connector tile (0,0,0) of the bus and board.
    const tripoint_bub_ms connector_pos = veh->bub_part_pos( here,
                                          veh->part( veh->part_with_feature(
                                                  tripoint_rel_ms( 0, 0, 0 ), "VERTICAL_TRAVERSAL", false ) ) );
    u.setpos( here, connector_pos );
    here.board_vehicle( connector_pos, &u );
    REQUIRE( u.in_vehicle );
    REQUIRE( u.posz() == 0 );

    // Climb up: executor handles it and reports true.
    CHECK( g->try_vehicle_deck_move( 1 ) );
    CHECK( u.posz() == 1 );
    CHECK( u.pos_bub().xy() == connector_pos.xy() );
    CHECK( u.in_vehicle );

    // Climb back down.
    CHECK( g->try_vehicle_deck_move( -1 ) );
    CHECK( u.posz() == 0 );
    CHECK( u.in_vehicle );
}

TEST_CASE( "try_vehicle_deck_move_declines_without_connector", "[vehicle][multifloor]" )
{
    clear_map();
    clear_avatar();
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    avatar &u = get_avatar();
    // Ground seat tile (0,1,0): boardable but no connector -> executor must decline (return false)
    // so the normal terrain-based vertical_move logic runs instead.
    const tripoint_bub_ms seat_pos = veh->bub_part_pos( here,
                                     veh->part( veh->part_with_feature(
                                             tripoint_rel_ms( 0, 1, 0 ), "SEAT", false ) ) );
    u.setpos( here, seat_pos );
    here.board_vehicle( seat_pos, &u );
    REQUIRE( u.posz() == 0 );

    CHECK_FALSE( g->try_vehicle_deck_move( 1 ) );
    CHECK( u.posz() == 0 );
}

TEST_CASE( "try_vehicle_deck_move_declines_off_vehicle", "[vehicle][multifloor]" )
{
    clear_map();
    clear_avatar();
    // No vehicle at the avatar's tile at all: executor declines, no crash.
    CHECK_FALSE( g->try_vehicle_deck_move( 1 ) );
    CHECK_FALSE( g->try_vehicle_deck_move( -1 ) );
}

TEST_CASE( "destroying_upper_floor_drops_the_rider", "[vehicle][multifloor]" )
{
    clear_map();
    clear_creatures();
    clear_avatar();
    map &here = get_map();
    // A prior test may have left the avatar parked on the bus origin tile; clear_avatar
    // resets stats but not position, so move it well clear of the spawn tiles.
    get_avatar().setpos( here, tripoint_bub_ms{ 0, 0, -2 } );
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    // The upper cargo/floor tile (1,0,1) sits over open air terrain at z=1.
    const int floor_idx = veh->part_with_feature( tripoint_rel_ms( 1, 0, 1 ), "BOARDABLE", false );
    REQUIRE( floor_idx >= 0 );
    const tripoint_bub_ms floor_pos = veh->bub_part_pos( here, veh->part( floor_idx ) );
    REQUIRE( floor_pos.z() == 1 );
    REQUIRE( here.is_open_air( floor_pos ) ); // terrain beneath the deck is open air

    // Put a zombie on that upper-deck floor. With the floor intact it must NOT fall.
    monster &zed = spawn_test_monster( "mon_zombie", floor_pos );
    zed.gravity_check( &here );
    REQUIRE( zed.pos_bub( here ) == floor_pos );

    // Destroy the whole upper floor tile, then let break_off run its recheck.
    // vehicle::damage_direct/break_off are private, so we go through the public
    // vehicle::damage() entry point; it spreads damage randomly across all parts
    // sharing the target's mount, and a part only tears off (break_off) once broken.
    // NOTE: valid_move refuses to drop a creature while ANY vehicle part still
    // occupies the tile -- a bare structural frame is a legitimate standing surface
    // even after the BOARDABLE deck floor is gone. So the rider only falls once the
    // tile is entirely vehicle-free. Hammer the mount's structural frame with massive
    // damage until break_off's structural branch clears every part on that mount
    // (the frame's removal cascades to the deck floor and cargo on the same tile).
    for( int i = 0; i < 500; ++i ) {
        const int idx = structure_part_at( *veh, tripoint_rel_ms( 1, 0, 1 ) );
        if( idx < 0 ) {
            break;
        }
        veh->damage( here, idx, 100000, damage_bash );
    }
    REQUIRE( veh->parts_at_relative( tripoint_rel_ms( 1, 0, 1 ), false ).empty() );

    // The tile is now open air with no vehicle floor, so the recheck must have
    // dropped the zombie off z=1.
    monster *still = get_creature_tracker().creature_at<monster>( floor_pos );
    CHECK( still == nullptr );
}

TEST_CASE( "destroying_ground_part_does_not_drop_rider", "[vehicle][multifloor]" )
{
    clear_map();
    clear_creatures();
    clear_avatar();
    map &here = get_map();
    // clear_avatar resets stats but not position; move the avatar clear of the spawn tile.
    get_avatar().setpos( here, tripoint_bub_ms{ 0, 0, -2 } );
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                     0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    const int seat = veh->part_with_feature( tripoint_rel_ms( 0, 0, 0 ), "BOARDABLE", false );
    REQUIRE( seat >= 0 );
    const tripoint_bub_ms seat_pos = veh->bub_part_pos( here, veh->part( seat ) );
    REQUIRE_FALSE( here.is_open_air( seat_pos ) );
    monster &zed = spawn_test_monster( "mon_zombie", seat_pos );
    here.vehicle_floor_removed_recheck( seat_pos );      // direct call: must be a no-op on solid ground
    CHECK( zed.pos_bub( here ) == seat_pos );
}

TEST_CASE( "avatar_boards_upper_deck_seat", "[vehicle][multifloor]" )
{
    clear_map();
    clear_avatar();
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    avatar &u = get_avatar();
    // Reach the upper deck the way gameplay does. A bare setpos to a z=1 tile cannot be
    // boarded reliably in a test: board_vehicle's trailing update_map() re-centres the
    // reality bubble on the argless Character::pos_bub(), which only the bubble-shifting
    // vertical_move path refreshes -- so a directly-placed avatar gets pulled back to z=0.
    // Climbing through the connector (Task 2's try_vehicle_deck_move) performs that shift,
    // building the z=1 caches and refreshing the cached position, exactly as real play.
    const int connector = veh->part_with_feature( tripoint_rel_ms( 0, 0, 0 ),
                          "VERTICAL_TRAVERSAL", false );
    REQUIRE( connector >= 0 );
    const tripoint_bub_ms connector_pos = veh->bub_part_pos( here, veh->part( connector ) );
    u.setpos( here, connector_pos );
    here.board_vehicle( connector_pos, &u );
    REQUIRE( u.in_vehicle );
    REQUIRE( u.pos_bub( here ).z() == 0 );

    REQUIRE( g->try_vehicle_deck_move( 1 ) );
    REQUIRE( u.pos_bub( here ).z() == 1 );

    // Now step onto the adjacent upper-deck seat (0,1,1) and board it. The bubble is at
    // z=1 now, so boarding behaves as in play.
    const int seat = veh->part_with_feature( tripoint_rel_ms( 0, 1, 1 ), "SEAT", false );
    REQUIRE( seat >= 0 );
    const tripoint_bub_ms seat_pos = veh->bub_part_pos( here, veh->part( seat ) );
    REQUIRE( seat_pos.z() == 1 );
    here.unboard_vehicle( u.pos_bub( here ) );
    u.setpos( here, seat_pos );
    here.board_vehicle( seat_pos, &u );
    CHECK( u.in_vehicle );
    CHECK( u.pos_bub( here ).z() == 1 );
    // The boarded part the map resolves for us is the upper-deck seat, not a ground part.
    const optional_vpart_position vp = here.veh_at( u.pos_bub( here ) );
    REQUIRE( vp );
    CHECK( vp->part_with_feature( "SEAT", false ).has_value() );
}

TEST_CASE( "two_floor_bus_upper_deck_collides_horizontally", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map(); // z-levels 0 and 1 present in the bubble

    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 100, 0 );
    REQUIRE( veh != nullptr );
    veh->check_falling_or_floating();
    REQUIRE_FALSE( veh->is_in_water() );

    // Pick the front-most (max abs x) upper-deck (mount.z == 1) structural part; the tile one
    // step ahead (+x) of it at the upper deck's own z-level is outside our footprint, so a wall
    // there is an obstacle only the upper deck can hit.
    int upper_idx = -1;
    tripoint_bub_ms upper_bub;
    for( const vpart_reference &vp : veh->get_all_parts() ) {
        if( vp.part().removed || vp.part().is_fake ) {
            continue;
        }
        if( vp.info().location != "structure" || vp.part().mount.z() != 1 ) {
            continue;
        }
        const tripoint_bub_ms p = vp.pos_bub( here );
        if( upper_idx < 0 || p.x() > upper_bub.x() ) {
            upper_idx = vp.part_index();
            upper_bub = p;
        }
    }
    REQUIRE( upper_idx >= 0 );
    REQUIRE( upper_bub.z() == 1 ); // the upper deck really is one z above the vehicle's base

    const tripoint_bub_ms wall_bub = upper_bub + tripoint_rel_ms( 1, 0, 0 );
    here.ter_set( wall_bub, ter_id( "t_wall" ) );
    REQUIRE( here.impassable( wall_bub ) );

    // Ask part_collision (the same entry teleport uses) to resolve that upper-deck part moving
    // horizontally into the wall tile. `vertical == false` is the truth for a horizontal move.
    // Pre-fix, part_collision ignored the caller and derived verticality from the part's absolute
    // z (z=1 != vehicle z=0 -> "vertical"), read vertical_velocity (0), and early-returned nothing
    // -> the deck phased through. The fix honours the passed move direction, so this is a real hit.
    veh->velocity = 400;
    veh->vertical_velocity = 0;
    const veh_collision coll = veh->part_collision( here, upper_idx, here.get_abs( wall_bub ),
                               false, false, false );
    CHECK( coll.type != veh_coll_nothing );
}

TEST_CASE( "wheels_touch_ground_only_on_deck_zero", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 100, 0 );
    REQUIRE( veh != nullptr );

    REQUIRE_FALSE( veh->wheelcache.empty() );
    for( const int w : veh->wheelcache ) {
        CAPTURE( w );
        CHECK( veh->part( w ).mount.z() == 0 );
    }
}

// M5 §6.3 cross term (mount.z x ramp): while a two-floor vehicle is transiting
// a ramp, advance_precalc_mounts records the ramp displacement into the
// transient precalc_z_delta (vehicle.cpp ~8867-8892), and precalc_mounts
// reseeds precalc.z = mount.z + precalc_z_delta (vehicle.cpp ~3807). This
// proves that composition holds for the upper deck too: at every tick, each
// upper-deck part must sit exactly one z above the lower-deck part sharing
// its (x,y) mount, i.e. the deck gap is invariant to ramp displacement.
TEST_CASE( "upper_deck_precalc_z_tracks_lower_deck_over_ramp", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();

    // Build a short up-ramp along the drive axis (mirror vehicle_ramp_test's terrain).
    const int y = 60;
    const int lowx = 66;
    const int highx = 67;
    here.ter_set( tripoint_bub_ms( lowx,  y, 0 ), ter_id( "t_ramp_up_low" ) );
    here.ter_set( tripoint_bub_ms( highx, y, 0 ), ter_id( "t_ramp_up_high" ) );
    here.ter_set( tripoint_bub_ms( lowx,  y, 1 ), ter_id( "t_ramp_down_low" ) );
    here.ter_set( tripoint_bub_ms( highx, y, 1 ), ter_id( "t_ramp_down_high" ) );

    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, y, 0 ), 0_degrees, 100, 0 );
    REQUIRE( veh != nullptr );
    veh->check_falling_or_floating();
    veh->tags.insert( "IN_CONTROL_OVERRIDE" );
    veh->engine_on = true;
    veh->cruise_velocity = 400;
    veh->velocity = 400;

    auto deck_gap_holds = [&]() {
        // Map every occupied (x,y) mount to the set of z's present there.
        for( const vpart_reference &up : veh->get_all_parts() ) {
            const vehicle_part &pu = up.part();
            if( pu.removed || pu.is_fake || pu.mount.z() != 1 ) {
                continue;
            }
            // find the lower-deck partner at the same (x,y)
            bool found = false;
            for( const vpart_reference &lo : veh->get_all_parts() ) {
                const vehicle_part &pl = lo.part();
                if( pl.removed || pl.is_fake || pl.mount.z() != 0 ) {
                    continue;
                }
                if( pl.mount.xy() == pu.mount.xy() ) {
                    CAPTURE( pu.mount.xy() );
                    CHECK( pu.precalc[0].z() == pl.precalc[0].z() + 1 );
                    found = true;
                }
            }
            CAPTURE( pu.mount.xy() );
            CHECK( found ); // every upper-deck tile has a lower-deck floor beneath it
        }
    };

    // Guard against silent degradation: the deck-gap invariant must be exercised WHILE a ramp
    // displacement is in flight (a part's precalc.z lifted off its mount deck), not only on flat
    // ground where precalc.z == mount.z for every part. (The whole-vehicle sm_pos.z() only flips
    // once the pivot crosses, which takes more travel than this short run; a lifted front part is
    // the earlier, sufficient signal that the ramp composition is actually being tested.)
    auto any_part_on_ramp = [&]() {
        for( const vpart_reference &vp : veh->get_all_parts() ) {
            const vehicle_part &p = vp.part();
            if( !p.removed && !p.is_fake && p.precalc[0].z() != p.mount.z() ) {
                return true;
            }
        }
        return false;
    };
    bool saw_ramp_displacement = false;
    for( int cycle = 0; cycle < 8; cycle++ ) {
        CAPTURE( cycle );
        deck_gap_holds();
        here.vehmove();
        saw_ramp_displacement = saw_ramp_displacement || any_part_on_ramp();
    }
    deck_gap_holds();
    REQUIRE( saw_ramp_displacement );
}

// M5 §5: the mass center stays PLANAR by design — calc_mass_center accumulates
// only x/y, so upper-deck mass folds into the same (x,y) center a one-floor
// vehicle of equal total mass/footprint would have, with no z term feeding into
// handling. local_center_of_mass returning a point_rel_ms (2D, not tripoint_rel_ms)
// is itself the structural proof; this test pins the value inside the bus's
// mount footprint so a later "add z to the mass center" change trips a red test.
TEST_CASE( "two_floor_bus_mass_center_is_planar", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 100, 0 );
    REQUIRE( veh != nullptr );

    const point_rel_ms com = veh->local_center_of_mass( here );
    CAPTURE( com );
    CHECK( std::isfinite( com.x() ) );
    CHECK( std::isfinite( com.y() ) );
    // Footprint bounds per the test_bus_2floor prototype: x in [-1, 2], y in [0, 1].
    CHECK( com.x() >= -1 );
    CHECK( com.x() <= 2 );
    CHECK( com.y() >= 0 );
    CHECK( com.y() <= 1 );
}

TEST_CASE( "frame_stacked_decks_connect_but_do_not_allow_climbing", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    const int ladder = veh->part_with_feature( tripoint_rel_ms( 0, 0, 0 ),
                       "VERTICAL_TRAVERSAL", false );
    REQUIRE( ladder >= 0 );
    veh->remove_part( veh->part( ladder ) );
    veh->find_and_split_vehicles( here, {} );

    // Still one vehicle with an upper deck: frames connect the decks structurally...
    bool still_has_upper = false;
    for( const vpart_reference &vpr : veh->get_all_parts() ) {
        if( !vpr.part().removed && vpr.part().mount.z() == 1 ) {
            still_has_upper = true;
            break;
        }
    }
    REQUIRE( still_has_upper );

    // ...but with the ladder gone there is no way to climb between the decks.
    CHECK_FALSE( veh->allows_deck_traversal( tripoint_rel_ms( 0, 0, 0 ), 1 ) );
    CHECK_FALSE( veh->allows_deck_traversal( tripoint_rel_ms( 0, 0, 1 ), -1 ) );
}

TEST_CASE( "removing_one_of_several_frame_bridges_does_not_split", "[vehicle][multifloor]" )
{
    // The bus stacks a frame on every upper tile, so the decks are connected at
    // multiple points. Removing a single ground frame under one stack must not split
    // the vehicle: the upper tile above it still reaches ground through neighbouring
    // stacks.
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    // Ground frame under the (1,1) stack; its upper (1,1,1) also neighbours upper
    // frames at (1,0,1) and (0,1,1), which keep their own ground frames.
    const int gi = structure_part_at( *veh, tripoint_rel_ms( 1, 1, 0 ) );
    REQUIRE( gi >= 0 );
    veh->remove_part( veh->part( gi ) );
    veh->find_and_split_vehicles( here, {} );

    int upper_tiles = 0;
    for( const vpart_reference &vpr : veh->get_all_parts() ) {
        if( !vpr.part().removed && vpr.part().mount.z() == 1 ) {
            upper_tiles++;
        }
    }
    // All four original upper tiles (0,0)/(0,1)/(1,0)/(1,1) remain on the one vehicle.
    CHECK( upper_tiles >= 4 );
}

// --- M6 Workstream A: 3D zone/label keys ---

TEST_CASE( "labels_on_different_decks_are_distinct", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                     0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    // Same (x,y), different deck. Under 2D keys the set treats these as equal and
    // silently keeps only one; under 3D keys both coexist.
    veh->labels.insert( label( tripoint_rel_ms( 0, 0, 0 ), "ground" ) );
    veh->labels.insert( label( tripoint_rel_ms( 0, 0, 1 ), "upper" ) );
    CHECK( veh->labels.size() == 2 );
    CHECK( veh->labels.count( label( tripoint_rel_ms( 0, 0, 0 ) ) ) == 1 );
    CHECK( veh->labels.count( label( tripoint_rel_ms( 0, 0, 1 ) ) ) == 1 );
}

TEST_CASE( "loot_zone_erase_is_per_deck", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                     0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    const zone_data z_ground( "g", zone_type_id( "LOOT_UNSORTED" ), your_fac,
                              false, true, tripoint_abs_ms::zero, tripoint_abs_ms::zero );
    const zone_data z_upper = z_ground;
    veh->loot_zones.emplace( tripoint_rel_ms( 0, 0, 0 ), z_ground );
    veh->loot_zones.emplace( tripoint_rel_ms( 0, 0, 1 ), z_upper );
    // Erasing the ground-deck key must leave the upper-deck zone intact (2D erase
    // by (0,0) would remove both).
    veh->loot_zones.erase( tripoint_rel_ms( 0, 0, 0 ) );
    CHECK( veh->loot_zones.count( tripoint_rel_ms( 0, 0, 0 ) ) == 0 );
    CHECK( veh->loot_zones.count( tripoint_rel_ms( 0, 0, 1 ) ) == 1 );
}

TEST_CASE( "label_z_roundtrip", "[vehicle][multifloor]" )
{
    const label l( tripoint_rel_ms( 1, 0, 1 ), "upper" );
    std::ostringstream os;
    JsonOut jout( os );
    l.serialize( jout );
    CHECK( os.str().find( "\"z\":1" ) != std::string::npos ); // non-zero z persisted

    JsonValue jv = json_loader::from_string( os.str() );
    label l2;
    l2.deserialize( jv.get_object() );
    CHECK( l2 == label( tripoint_rel_ms( 1, 0, 1 ) ) );
    CHECK( l2.text == "upper" );
}

TEST_CASE( "single_floor_label_omits_z", "[vehicle][multifloor]" )
{
    const label l( tripoint_rel_ms( 1, 0, 0 ), "ground" );
    std::ostringstream os;
    JsonOut jout( os );
    l.serialize( jout );
    // z==0 must not emit a "z" member (byte-identical guard).
    CHECK( os.str().find( "\"z\":" ) == std::string::npos );
}

TEST_CASE( "vehicle_zone_z_roundtrip", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                     0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    const zone_data z_upper( "u", zone_type_id( "LOOT_UNSORTED" ), your_fac,
                             false, true, tripoint_abs_ms::zero, tripoint_abs_ms::zero );
    veh->loot_zones.emplace( tripoint_rel_ms( 1, 0, 1 ), z_upper );

    std::ostringstream os;
    JsonOut jout( os );
    veh->serialize( jout );
    CHECK( os.str().find( "\"z\":1" ) != std::string::npos ); // zone deck persisted

    JsonValue jv = json_loader::from_string( os.str() );
    vehicle after{ vproto_id() };
    after.deserialize( jv.get_object() );
    CHECK( after.loot_zones.count( tripoint_rel_ms( 1, 0, 1 ) ) == 1 );
    CHECK( after.loot_zones.count( tripoint_rel_ms( 1, 0, 0 ) ) == 0 );
}

TEST_CASE( "multideck_zones_labels_survive_planar_rekey", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    // Same (x,y), different deck: the exact collision 2D keys corrupt.
    veh->labels.insert( label( tripoint_rel_ms( 0, 0, 0 ), "ground" ) );
    veh->labels.insert( label( tripoint_rel_ms( 0, 0, 1 ), "upper" ) );
    const zone_data zd( "z", zone_type_id( "LOOT_UNSORTED" ), your_fac,
                        false, true, tripoint_abs_ms::zero, tripoint_abs_ms::zero );
    veh->loot_zones.emplace( tripoint_rel_ms( 0, 0, 0 ), zd );
    veh->loot_zones.emplace( tripoint_rel_ms( 0, 0, 1 ), zd );
    REQUIRE( veh->labels.size() == 2 );
    REQUIRE( veh->loot_zones.size() == 2 );

    // shift_parts re-keys every label and zone by a planar delta. Deck (z) must be
    // preserved and the two decks must stay distinct (2D keys would collapse them).
    veh->shift_parts( here, point_rel_ms( 1, 0 ) );

    REQUIRE( veh->labels.size() == 2 );
    int ground_z = -99;
    int upper_z = -99;
    for( const label &l : veh->labels ) {
        if( l.text == "ground" ) {
            ground_z = l.z();
        } else if( l.text == "upper" ) {
            upper_z = l.z();
        }
    }
    CHECK( ground_z == 0 ); // ground label stayed on the ground deck
    CHECK( upper_z == 1 );  // upper label stayed on the upper deck

    REQUIRE( veh->loot_zones.size() == 2 );
    int zone_z_sum = 0;
    for( const auto &z : veh->loot_zones ) {
        zone_z_sum += z.first.z();
    }
    CHECK( zone_z_sum == 1 ); // exactly one zone at z=0 and one at z=1
}
