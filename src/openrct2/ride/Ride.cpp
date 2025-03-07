/*****************************************************************************
 * Copyright (c) 2014-2019 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "Ride.h"

#include "../Cheats.h"
#include "../Context.h"
#include "../Editor.h"
#include "../Game.h"
#include "../Input.h"
#include "../OpenRCT2.h"
#include "../actions/RideEntranceExitRemoveAction.hpp"
#include "../actions/RideSetSetting.hpp"
#include "../actions/RideSetStatus.hpp"
#include "../actions/RideSetVehiclesAction.hpp"
#include "../actions/TrackRemoveAction.hpp"
#include "../audio/AudioMixer.h"
#include "../audio/audio.h"
#include "../common.h"
#include "../config/Config.h"
#include "../core/Guard.hpp"
#include "../core/Optional.hpp"
#include "../interface/Window.h"
#include "../localisation/Date.h"
#include "../localisation/Localisation.h"
#include "../management/Finance.h"
#include "../management/Marketing.h"
#include "../management/NewsItem.h"
#include "../network/network.h"
#include "../object/ObjectList.h"
#include "../object/ObjectManager.h"
#include "../object/StationObject.h"
#include "../paint/VirtualFloor.h"
#include "../peep/Peep.h"
#include "../peep/Staff.h"
#include "../rct1/RCT1.h"
#include "../scenario/Scenario.h"
#include "../ui/UiContext.h"
#include "../ui/WindowManager.h"
#include "../util/Util.h"
#include "../windows/Intent.h"
#include "../world/Banner.h"
#include "../world/Climate.h"
#include "../world/Footpath.h"
#include "../world/Location.hpp"
#include "../world/Map.h"
#include "../world/MapAnimation.h"
#include "../world/Park.h"
#include "../world/Scenery.h"
#include "../world/Sprite.h"
#include "CableLift.h"
#include "MusicList.h"
#include "RideData.h"
#include "RideGroupManager.h"
#include "ShopItem.h"
#include "Station.h"
#include "Track.h"
#include "TrackData.h"
#include "TrackDesign.h"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdlib>
#include <iterator>
#include <limits>

using namespace OpenRCT2;

uint8_t gTypeToRideEntryIndexMap[TYPE_TO_RIDE_ENTRY_SLOTS];
static constexpr const int32_t RideInspectionInterval[] = {
    10, 20, 30, 45, 60, 120, 0, 0,
};

static std::vector<Ride> _rides;

bool gGotoStartPlacementMode = false;

money16 gTotalRideValueForMoney;

money32 _currentTrackPrice;

uint16_t _numCurrentPossibleRideConfigurations;
uint16_t _numCurrentPossibleSpecialTrackPieces;

uint16_t _currentTrackCurve;
uint8_t _rideConstructionState;
ride_id_t _currentRideIndex;

CoordsXYZ _currentTrackBegin;

uint8_t _currentTrackPieceDirection;
track_type_t _currentTrackPieceType;
uint8_t _currentTrackSelectionFlags;
int8_t _rideConstructionArrowPulseTime;
uint8_t _currentTrackSlopeEnd;
uint8_t _currentTrackBankEnd;
uint8_t _currentTrackLiftHill;
uint8_t _currentTrackAlternative;
track_type_t _selectedTrackType;

uint8_t _previousTrackBankEnd;
uint8_t _previousTrackSlopeEnd;

CoordsXYZ _previousTrackPiece;

uint8_t _currentBrakeSpeed2;
uint8_t _currentSeatRotationAngle;

CoordsXYZD _unkF440C5;

uint8_t gRideEntranceExitPlaceType;
ride_id_t gRideEntranceExitPlaceRideIndex;
uint8_t gRideEntranceExitPlaceStationIndex;
uint8_t gRideEntranceExitPlacePreviousRideConstructionState;
uint8_t gRideEntranceExitPlaceDirection;

uint8_t gLastEntranceStyle;

// Static function declarations
Peep* find_closest_mechanic(int32_t x, int32_t y, int32_t forInspection);
static void ride_breakdown_status_update(Ride* ride);
static void ride_breakdown_update(Ride* ride);
static void ride_call_closest_mechanic(Ride* ride);
static void ride_call_mechanic(Ride* ride, Peep* mechanic, int32_t forInspection);
static void ride_entrance_exit_connected(Ride* ride);
static int32_t ride_get_new_breakdown_problem(Ride* ride);
static void ride_inspection_update(Ride* ride);
static void ride_mechanic_status_update(Ride* ride, int32_t mechanicStatus);
static void ride_music_update(Ride* ride);
static void ride_shop_connected(Ride* ride);
void loc_6DDF9C(Ride* ride, TileElement* tileElement);

RideManager GetRideManager()
{
    return {};
}

size_t RideManager::size() const
{
    size_t count = 0;
    for (size_t i = 0; i < _rides.size(); i++)
    {
        if (_rides[i].type != RIDE_TYPE_NULL)
        {
            count++;
        }
    }
    return count;
}

RideManager::Iterator RideManager::begin()
{
    return RideManager::Iterator(*this, 0, _rides.size());
}

RideManager::Iterator RideManager::end()
{
    return RideManager::Iterator(*this, _rides.size(), _rides.size());
}

ride_id_t GetNextFreeRideId()
{
    size_t result = _rides.size();
    for (size_t i = 0; i < _rides.size(); i++)
    {
        if (_rides[i].type == RIDE_TYPE_NULL)
        {
            result = i;
            break;
        }
    }
    if (result >= RIDE_ID_NULL)
    {
        return RIDE_ID_NULL;
    }
    return (ride_id_t)result;
}

Ride* GetOrAllocateRide(ride_id_t index)
{
    if (_rides.size() <= index)
    {
        _rides.resize(index + 1);
    }

    auto result = &_rides[index];
    result->id = index;
    return result;
}

Ride* get_ride(ride_id_t index)
{
    if (index < _rides.size())
    {
        auto& ride = _rides[index];
        if (ride.type != RIDE_TYPE_NULL)
        {
            assert(ride.id == index);
            return &ride;
        }
    }
    return nullptr;
}

rct_ride_entry* get_ride_entry(int32_t index)
{
    rct_ride_entry* result = nullptr;
    auto& objMgr = OpenRCT2::GetContext()->GetObjectManager();

    auto obj = objMgr.GetLoadedObject(OBJECT_TYPE_RIDE, index);
    if (obj != nullptr)
    {
        result = (rct_ride_entry*)obj->GetLegacyData();
    }

    return result;
}

std::string_view get_ride_entry_name(size_t index)
{
    if (index >= (size_t)object_entry_group_counts[OBJECT_TYPE_RIDE])
    {
        log_error("invalid index %d for ride type", index);
        return {};
    }

    auto objectEntry = object_entry_get_entry(OBJECT_TYPE_RIDE, index);
    if (objectEntry != nullptr)
    {
        return objectEntry->GetName();
    }
    return {};
}

rct_ride_entry* Ride::GetRideEntry() const
{
    return get_ride_entry(subtype);
}

/**
 *
 *  rct2: 0x006DED68
 */
void reset_type_to_ride_entry_index_map(IObjectManager& objectManager)
{
    size_t stride = MAX_RIDE_OBJECTS + 1;
    uint8_t* entryTypeTable = (uint8_t*)malloc(RIDE_TYPE_COUNT * stride);
    std::fill_n(entryTypeTable, RIDE_TYPE_COUNT * stride, 0xFF);

    for (uint8_t i = 0; i < MAX_RIDE_OBJECTS; i++)
    {
        auto obj = objectManager.GetLoadedObject(OBJECT_TYPE_RIDE, i);
        if (obj != nullptr)
        {
            for (uint8_t j = 0; j < MAX_RIDE_TYPES_PER_RIDE_ENTRY; j++)
            {
                auto rideEntry = (rct_ride_entry*)obj->GetLegacyData();
                uint8_t rideType = rideEntry->ride_type[j];
                if (rideType < RIDE_TYPE_COUNT)
                {
                    uint8_t* entryArray = &entryTypeTable[rideType * stride];
                    uint8_t* nextEntry = (uint8_t*)memchr(entryArray, 0xFF, stride);
                    *nextEntry = i;
                }
            }
        }
    }

    uint8_t* dst = gTypeToRideEntryIndexMap;
    for (uint8_t i = 0; i < RIDE_TYPE_COUNT; i++)
    {
        uint8_t* entryArray = &entryTypeTable[i * stride];
        uint8_t* entry = entryArray;
        while (*entry != 0xFF)
        {
            *dst++ = *entry++;
        }
        *dst++ = 0xFF;
    }

    free(entryTypeTable);
}

uint8_t* get_ride_entry_indices_for_ride_type(uint8_t rideType)
{
    uint8_t* entryIndexList = gTypeToRideEntryIndexMap;
    while (rideType > 0)
    {
        do
        {
            entryIndexList++;
        } while (*(entryIndexList - 1) != RIDE_ENTRY_INDEX_NULL);
        rideType--;
    }
    return entryIndexList;
}

int32_t ride_get_count()
{
    return (int32_t)GetRideManager().size();
}

int32_t Ride::GetTotalQueueLength() const
{
    int32_t i, queueLength = 0;
    for (i = 0; i < MAX_STATIONS; i++)
        if (!ride_get_entrance_location(this, i).isNull())
            queueLength += stations[i].QueueLength;
    return queueLength;
}

int32_t Ride::GetMaxQueueTime() const
{
    uint8_t i, queueTime = 0;
    for (i = 0; i < MAX_STATIONS; i++)
        if (!ride_get_entrance_location(this, i).isNull())
            queueTime = std::max(queueTime, stations[i].QueueTime);
    return (int32_t)queueTime;
}

Peep* Ride::GetQueueHeadGuest(int32_t stationIndex) const
{
    Peep* peep;
    Peep* result = nullptr;
    uint16_t spriteIndex = stations[stationIndex].LastPeepInQueue;
    while ((peep = try_get_guest(spriteIndex)) != nullptr)
    {
        spriteIndex = peep->next_in_queue;
        result = peep;
    }
    return result;
}

void Ride::UpdateQueueLength(int32_t stationIndex)
{
    uint16_t count = 0;
    Peep* peep;
    uint16_t spriteIndex = stations[stationIndex].LastPeepInQueue;
    while ((peep = try_get_guest(spriteIndex)) != nullptr)
    {
        spriteIndex = peep->next_in_queue;
        count++;
    }
    stations[stationIndex].QueueLength = count;
}

void Ride::QueueInsertGuestAtFront(int32_t stationIndex, Peep* peep)
{
    assert(stationIndex < MAX_STATIONS);
    assert(peep != nullptr);

    peep->next_in_queue = SPRITE_INDEX_NULL;
    Peep* queueHeadGuest = GetQueueHeadGuest(peep->current_ride_station);
    if (queueHeadGuest == nullptr)
    {
        stations[peep->current_ride_station].LastPeepInQueue = peep->sprite_index;
    }
    else
    {
        queueHeadGuest->next_in_queue = peep->sprite_index;
    }
    UpdateQueueLength(peep->current_ride_station);
}

/**
 *
 *  rct2: 0x006AC916
 */
void ride_update_favourited_stat()
{
    uint16_t spriteIndex;
    Peep* peep;

    for (auto& ride : GetRideManager())
        ride.guests_favourite = 0;

    FOR_ALL_PEEPS (spriteIndex, peep)
    {
        if (peep->linked_list_index != SPRITE_LIST_PEEP)
            return;
        if (peep->favourite_ride != RIDE_ID_NULL)
        {
            auto ride = get_ride(peep->favourite_ride);
            if (ride != nullptr)
            {
                ride->guests_favourite++;
                ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_CUSTOMER;
            }
        }
    }

    window_invalidate_by_class(WC_RIDE_LIST);
}

/**
 *
 *  rct2: 0x006AC3AB
 */
money32 Ride::CalculateIncomePerHour() const
{
    // Get entry by ride to provide better reporting
    rct_ride_entry* entry = GetRideEntry();
    if (entry == nullptr)
    {
        return 0;
    }
    money32 customersPerHour = ride_customers_per_hour(this);
    money32 priceMinusCost = ride_get_price(this);

    int32_t currentShopItem = entry->shop_item;
    if (currentShopItem != SHOP_ITEM_NONE)
    {
        priceMinusCost -= ShopItems[currentShopItem].Cost;
    }

    currentShopItem = (lifecycle_flags & RIDE_LIFECYCLE_ON_RIDE_PHOTO) ? RidePhotoItems[type] : entry->shop_item_secondary;

    if (currentShopItem != SHOP_ITEM_NONE)
    {
        priceMinusCost += price_secondary;
        priceMinusCost -= ShopItems[currentShopItem].Cost;

        if (entry->shop_item != SHOP_ITEM_NONE)
            priceMinusCost /= 2;
    }

    return customersPerHour * priceMinusCost;
}

/**
 *
 *  rct2: 0x006CAF80
 * ax result x
 * bx result y
 * dl ride index
 * esi result map element
 */
bool ride_try_get_origin_element(const Ride* ride, CoordsXYE* output)
{
    TileElement* resultTileElement = nullptr;

    tile_element_iterator it;
    tile_element_iterator_begin(&it);
    do
    {
        if (it.element->GetType() != TILE_ELEMENT_TYPE_TRACK)
            continue;
        if (it.element->AsTrack()->GetRideIndex() != ride->id)
            continue;

        // Found a track piece for target ride

        // Check if it's not the station or ??? (but allow end piece of station)
        bool specialTrackPiece
            = (it.element->AsTrack()->GetTrackType() != TRACK_ELEM_BEGIN_STATION
               && it.element->AsTrack()->GetTrackType() != TRACK_ELEM_MIDDLE_STATION
               && (TrackSequenceProperties[it.element->AsTrack()->GetTrackType()][0] & TRACK_SEQUENCE_FLAG_ORIGIN));

        // Set result tile to this track piece if first found track or a ???
        if (resultTileElement == nullptr || specialTrackPiece)
        {
            resultTileElement = it.element;

            if (output != nullptr)
            {
                output->element = resultTileElement;
                output->x = it.x * 32;
                output->y = it.y * 32;
            }
        }

        if (specialTrackPiece)
        {
            return true;
        }
    } while (tile_element_iterator_next(&it));

    return resultTileElement != nullptr;
}

/**
 *
 * rct2: 0x006C6096
 * Gets the next track block coordinates from the
 * coordinates of the first of element of a track block.
 * Use track_block_get_next if you are unsure if you are
 * on the first element of a track block
 */
bool track_block_get_next_from_zero(
    int16_t x, int16_t y, int16_t z_start, Ride* ride, uint8_t direction_start, CoordsXYE* output, int32_t* z,
    int32_t* direction, bool isGhost)
{
    if (!(direction_start & (1 << 2)))
    {
        x += CoordsDirectionDelta[direction_start].x;
        y += CoordsDirectionDelta[direction_start].y;
    }

    TileElement* tileElement = map_get_first_element_at({ x, y });
    if (tileElement == nullptr)
    {
        output->element = nullptr;
        output->x = LOCATION_NULL;
        return false;
    }

    do
    {
        auto trackElement = tileElement->AsTrack();
        if (trackElement == nullptr)
            continue;

        if (trackElement->GetRideIndex() != ride->id)
            continue;

        if (trackElement->GetSequenceIndex() != 0)
            continue;

        if (tileElement->IsGhost() != isGhost)
            continue;

        auto nextTrackBlock = get_track_def_from_ride(ride, trackElement->GetTrackType());
        if (nextTrackBlock == nullptr)
            continue;

        auto nextTrackCoordinate = get_track_coord_from_ride(ride, trackElement->GetTrackType());
        if (nextTrackCoordinate == nullptr)
            continue;

        uint8_t nextRotation = tileElement->GetDirectionWithOffset(nextTrackCoordinate->rotation_begin)
            | (nextTrackCoordinate->rotation_begin & (1 << 2));

        if (nextRotation != direction_start)
            continue;

        int16_t nextZ = nextTrackCoordinate->z_begin - nextTrackBlock->z + tileElement->GetBaseZ();
        if (nextZ != z_start)
            continue;

        if (z != nullptr)
            *z = tileElement->GetBaseZ();
        if (direction != nullptr)
            *direction = nextRotation;
        output->x = x;
        output->y = y;
        output->element = tileElement;
        return true;
    } while (!(tileElement++)->IsLastForTile());

    if (direction != nullptr)
        *direction = direction_start;
    if (z != nullptr)
        *z = z_start;
    output->x = x;
    output->y = y;
    output->element = --tileElement;
    return false;
}

/**
 *
 *  rct2: 0x006C60C2
 */
bool track_block_get_next(CoordsXYE* input, CoordsXYE* output, int32_t* z, int32_t* direction)
{
    auto inputElement = input->element->AsTrack();
    if (inputElement == nullptr)
        return false;

    auto rideIndex = inputElement->GetRideIndex();
    auto ride = get_ride(rideIndex);
    if (ride == nullptr)
        return false;

    auto trackBlock = get_track_def_from_ride(ride, inputElement->GetTrackType());
    if (trackBlock == nullptr)
        return false;

    trackBlock += inputElement->GetSequenceIndex();

    auto trackCoordinate = get_track_coord_from_ride(ride, inputElement->GetTrackType());
    if (trackCoordinate == nullptr)
        return false;

    int32_t x = input->x;
    int32_t y = input->y;
    int32_t OriginZ = inputElement->GetBaseZ();

    uint8_t rotation = inputElement->GetDirection();

    CoordsXY coords = { x, y };
    CoordsXY trackCoordOffset = { trackCoordinate->x, trackCoordinate->y };
    CoordsXY trackBlockOffset = { trackBlock->x, trackBlock->y };
    coords += trackCoordOffset.Rotate(rotation);
    coords += trackBlockOffset.Rotate(direction_reverse(rotation));

    OriginZ -= trackBlock->z;
    OriginZ += trackCoordinate->z_end;

    uint8_t directionStart = ((trackCoordinate->rotation_end + rotation) & TILE_ELEMENT_DIRECTION_MASK)
        | (trackCoordinate->rotation_end & (1 << 2));

    return track_block_get_next_from_zero(coords.x, coords.y, OriginZ, ride, directionStart, output, z, direction, false);
}

/**
 * Returns the begin position / direction and end position / direction of the
 * track piece that proceeds the given location. Gets the previous track block
 * coordinates from the coordinates of the first of element of a track block.
 * Use track_block_get_previous if you are unsure if you are on the first
 * element of a track block
 *  rct2: 0x006C63D6
 */
bool track_block_get_previous_from_zero(
    int16_t x, int16_t y, int16_t z, Ride* ride, uint8_t direction, track_begin_end* outTrackBeginEnd)
{
    uint8_t directionStart = direction;
    direction = direction_reverse(direction);

    if (!(direction & (1 << 2)))
    {
        x += CoordsDirectionDelta[direction].x;
        y += CoordsDirectionDelta[direction].y;
    }

    TileElement* tileElement = map_get_first_element_at({ x, y });
    if (tileElement == nullptr)
    {
        outTrackBeginEnd->end_x = x;
        outTrackBeginEnd->end_y = y;
        outTrackBeginEnd->begin_element = nullptr;
        outTrackBeginEnd->begin_direction = direction_reverse(directionStart);
        return false;
    }

    do
    {
        auto trackElement = tileElement->AsTrack();
        if (trackElement == nullptr)
            continue;

        if (trackElement->GetRideIndex() != ride->id)
            continue;

        auto nextTrackBlock = get_track_def_from_ride(ride, trackElement->GetTrackType());
        if (nextTrackBlock == nullptr)
            continue;

        auto nextTrackCoordinate = get_track_coord_from_ride(ride, trackElement->GetTrackType());
        if (nextTrackCoordinate == nullptr)
            continue;

        nextTrackBlock += trackElement->GetSequenceIndex();
        if ((nextTrackBlock + 1)->index != 255)
            continue;

        uint8_t nextRotation = tileElement->GetDirectionWithOffset(nextTrackCoordinate->rotation_end)
            | (nextTrackCoordinate->rotation_end & (1 << 2));

        if (nextRotation != directionStart)
            continue;

        int16_t nextZ = nextTrackCoordinate->z_end - nextTrackBlock->z + tileElement->GetBaseZ();
        if (nextZ != z)
            continue;

        nextRotation = tileElement->GetDirectionWithOffset(nextTrackCoordinate->rotation_begin)
            | (nextTrackCoordinate->rotation_begin & (1 << 2));
        outTrackBeginEnd->begin_element = tileElement;
        outTrackBeginEnd->begin_x = x;
        outTrackBeginEnd->begin_y = y;
        outTrackBeginEnd->end_x = x;
        outTrackBeginEnd->end_y = y;

        CoordsXY coords = { outTrackBeginEnd->begin_x, outTrackBeginEnd->begin_y };
        CoordsXY offsets = { nextTrackCoordinate->x, nextTrackCoordinate->y };
        coords += offsets.Rotate(direction_reverse(nextRotation));
        outTrackBeginEnd->begin_x = coords.x;
        outTrackBeginEnd->begin_y = coords.y;

        outTrackBeginEnd->begin_z = tileElement->GetBaseZ();

        auto nextTrackBlock2 = get_track_def_from_ride(ride, trackElement->GetTrackType());
        if (nextTrackBlock2 == nullptr)
            continue;

        outTrackBeginEnd->begin_z += nextTrackBlock2->z - nextTrackBlock->z;
        outTrackBeginEnd->begin_direction = nextRotation;
        outTrackBeginEnd->end_direction = direction_reverse(directionStart);
        return true;
    } while (!(tileElement++)->IsLastForTile());

    outTrackBeginEnd->end_x = x;
    outTrackBeginEnd->end_y = y;
    outTrackBeginEnd->begin_z = z;
    outTrackBeginEnd->begin_element = nullptr;
    outTrackBeginEnd->end_direction = direction_reverse(directionStart);
    return false;
}

/**
 *
 *  rct2: 0x006C6402
 *
 * @remarks outTrackBeginEnd.begin_x and outTrackBeginEnd.begin_y will be in the
 * higher two bytes of ecx and edx where as outTrackBeginEnd.end_x and
 * outTrackBeginEnd.end_y will be in the lower two bytes (cx and dx).
 */
bool track_block_get_previous(int32_t x, int32_t y, TileElement* tileElement, track_begin_end* outTrackBeginEnd)
{
    auto trackElement = tileElement->AsTrack();
    if (trackElement == nullptr)
        return false;

    auto rideIndex = trackElement->GetRideIndex();
    auto ride = get_ride(rideIndex);
    if (ride == nullptr)
        return false;

    auto trackBlock = get_track_def_from_ride(ride, trackElement->GetTrackType());
    if (trackBlock == nullptr)
        return false;

    trackBlock += trackElement->GetSequenceIndex();

    auto trackCoordinate = get_track_coord_from_ride(ride, trackElement->GetTrackType());
    if (trackCoordinate == nullptr)
        return false;

    int32_t z = trackElement->GetBaseZ();

    uint8_t rotation = trackElement->GetDirection();
    CoordsXY coords = { x, y };
    CoordsXY offsets = { trackBlock->x, trackBlock->y };
    coords += offsets.Rotate(direction_reverse(rotation));

    z -= trackBlock->z;
    z += trackCoordinate->z_begin;

    rotation = ((trackCoordinate->rotation_begin + rotation) & TILE_ELEMENT_DIRECTION_MASK)
        | (trackCoordinate->rotation_begin & (1 << 2));

    return track_block_get_previous_from_zero(coords.x, coords.y, z, ride, rotation, outTrackBeginEnd);
}

/**
 *
 * Make sure to pass in the x and y of the start track element too.
 *  rct2: 0x006CB02F
 * ax result x
 * bx result y
 * esi input / output map element
 */
int32_t ride_find_track_gap(const Ride* ride, CoordsXYE* input, CoordsXYE* output)
{
    if (ride == nullptr || input == nullptr || input->element == nullptr
        || input->element->GetType() != TILE_ELEMENT_TYPE_TRACK)
        return 0;

    if (ride->type == RIDE_TYPE_MAZE)
    {
        return 0;
    }

    rct_window* w = window_find_by_class(WC_RIDE_CONSTRUCTION);
    if (w != nullptr && _rideConstructionState != RIDE_CONSTRUCTION_STATE_0 && _currentRideIndex == ride->id)
    {
        ride_construction_invalidate_current_track();
    }

    bool moveSlowIt = true;
    track_circuit_iterator it = {};
    track_circuit_iterator_begin(&it, *input);
    track_circuit_iterator slowIt = it;
    while (track_circuit_iterator_next(&it))
    {
        if (!track_is_connected_by_shape(it.last.element, it.current.element))
        {
            *output = it.current;
            return 1;
        }
        //#2081: prevent an infinite loop
        moveSlowIt = !moveSlowIt;
        if (moveSlowIt)
        {
            track_circuit_iterator_next(&slowIt);
            if (track_circuit_iterators_match(&it, &slowIt))
            {
                *output = it.current;
                return 1;
            }
        }
    }
    if (!it.looped)
    {
        *output = it.last;
        return 1;
    }

    return 0;
}

void Ride::FormatStatusTo(void* argsV) const
{
    auto args = (uint8_t*)argsV;

    if (lifecycle_flags & RIDE_LIFECYCLE_CRASHED)
    {
        set_format_arg_on(args, 0, rct_string_id, STR_CRASHED);
    }
    else if (lifecycle_flags & RIDE_LIFECYCLE_BROKEN_DOWN)
    {
        set_format_arg_on(args, 0, rct_string_id, STR_BROKEN_DOWN);
    }
    else if (status == RIDE_STATUS_CLOSED)
    {
        set_format_arg_on(args, 0, rct_string_id, STR_CLOSED);
        if (!ride_type_has_flag(type, RIDE_TYPE_FLAG_IS_SHOP))
        {
            if (num_riders != 0)
            {
                set_format_arg_on(args, 0, rct_string_id, num_riders == 1 ? STR_CLOSED_WITH_PERSON : STR_CLOSED_WITH_PEOPLE);
                set_format_arg_on(args, 2, uint16_t, num_riders);
            }
        }
    }
    else if (status == RIDE_STATUS_SIMULATING)
    {
        set_format_arg_on(args, 0, rct_string_id, STR_SIMULATING);
    }
    else if (status == RIDE_STATUS_TESTING)
    {
        set_format_arg_on(args, 0, rct_string_id, STR_TEST_RUN);
    }
    else if (
        mode == RIDE_MODE_RACE && !(lifecycle_flags & RIDE_LIFECYCLE_PASS_STATION_NO_STOPPING)
        && race_winner != SPRITE_INDEX_NULL)
    {
        auto sprite = get_sprite(race_winner);
        if (sprite != nullptr && sprite->IsPeep())
        {
            auto peep = sprite->AsPeep();
            set_format_arg_on(args, 0, rct_string_id, STR_RACE_WON_BY);
            peep->FormatNameTo(args + 2);
        }
        else
        {
            set_format_arg_on(args, 0, rct_string_id, STR_RACE_WON_BY);
            set_format_arg_on(args, 2, rct_string_id, STR_NONE);
        }
    }
    else if (!ride_type_has_flag(type, RIDE_TYPE_FLAG_IS_SHOP))
    {
        set_format_arg_on(args, 0, rct_string_id, num_riders == 1 ? STR_PERSON_ON_RIDE : STR_PEOPLE_ON_RIDE);
        set_format_arg_on(args, 2, uint16_t, num_riders);
    }
    else
    {
        set_format_arg_on(args, 0, rct_string_id, STR_OPEN);
    }
}

int32_t ride_get_total_length(const Ride* ride)
{
    int32_t i, totalLength = 0;
    for (i = 0; i < ride->num_stations; i++)
        totalLength += ride->stations[i].SegmentLength;
    return totalLength;
}

int32_t ride_get_total_time(Ride* ride)
{
    int32_t i, totalTime = 0;
    for (i = 0; i < ride->num_stations; i++)
        totalTime += ride->stations[i].SegmentTime;
    return totalTime;
}

bool Ride::CanHaveMultipleCircuits() const
{
    if (!(RideData4[type].flags & RIDE_TYPE_FLAG4_ALLOW_MULTIPLE_CIRCUITS))
        return false;

    // Only allow circuit or launch modes
    if (mode != RIDE_MODE_CONTINUOUS_CIRCUIT && mode != RIDE_MODE_REVERSE_INCLINE_LAUNCHED_SHUTTLE
        && mode != RIDE_MODE_POWERED_LAUNCH_PASSTROUGH)
    {
        return false;
    }

    // Must have no more than one vehicle and one station
    if (num_vehicles > 1 || num_stations > 1)
        return false;

    return true;
}

bool Ride::SupportsStatus(int32_t s) const
{
    switch (s)
    {
        case RIDE_STATUS_CLOSED:
        case RIDE_STATUS_OPEN:
            return true;
        case RIDE_STATUS_SIMULATING:
            return (
                !ride_type_has_flag(type, RIDE_TYPE_FLAG_NO_TEST_MODE) && ride_type_has_flag(type, RIDE_TYPE_FLAG_HAS_TRACK));
        case RIDE_STATUS_TESTING:
            return !ride_type_has_flag(type, RIDE_TYPE_FLAG_NO_TEST_MODE);
        default:
            return false;
    }
}

#pragma region Initialisation functions

/**
 *
 *  rct2: 0x006ACA89
 */
void ride_init_all()
{
    _rides.clear();
    _rides.shrink_to_fit();
}

/**
 *
 *  rct2: 0x006B7A38
 */
void reset_all_ride_build_dates()
{
    for (auto& ride : GetRideManager())
        ride.build_date -= gDateMonthsElapsed;
}

#pragma endregion

#pragma region Construction

static int32_t ride_check_if_construction_allowed(Ride* ride)
{
    rct_ride_entry* rideEntry = ride->GetRideEntry();
    if (rideEntry == nullptr)
    {
        context_show_error(STR_INVALID_RIDE_TYPE, STR_CANT_EDIT_INVALID_RIDE_TYPE);
        return 0;
    }
    if (ride->lifecycle_flags & RIDE_LIFECYCLE_BROKEN_DOWN)
    {
        ride->FormatNameTo(gCommonFormatArgs + 6);
        context_show_error(STR_CANT_START_CONSTRUCTION_ON, STR_HAS_BROKEN_DOWN_AND_REQUIRES_FIXING);
        return 0;
    }

    if (ride->status != RIDE_STATUS_CLOSED && ride->status != RIDE_STATUS_SIMULATING)
    {
        ride->FormatNameTo(gCommonFormatArgs + 6);
        context_show_error(STR_CANT_START_CONSTRUCTION_ON, STR_MUST_BE_CLOSED_FIRST);
        return 0;
    }

    return 1;
}

static rct_window* ride_create_or_find_construction_window(ride_id_t rideIndex)
{
    auto windowManager = GetContext()->GetUiContext()->GetWindowManager();
    auto intent = Intent(INTENT_ACTION_RIDE_CONSTRUCTION_FOCUS);
    intent.putExtra(INTENT_EXTRA_RIDE_ID, rideIndex);
    windowManager->BroadcastIntent(intent);
    return window_find_by_class(WC_RIDE_CONSTRUCTION);
}

/**
 *
 *  rct2: 0x006B4857
 */
void ride_construct(Ride* ride)
{
    CoordsXYE trackElement;
    if (ride_try_get_origin_element(ride, &trackElement))
    {
        ride_find_track_gap(ride, &trackElement, &trackElement);

        rct_window* w = window_get_main();
        if (w != nullptr && ride_modify(&trackElement))
            window_scroll_to_location(w, trackElement.x, trackElement.y, trackElement.element->GetBaseZ());
    }
    else
    {
        ride_initialise_construction_window(ride);
    }
}

/**
 *
 *  rct2: 0x006DD4D5
 */
static void ride_remove_cable_lift(Ride* ride)
{
    if (ride->lifecycle_flags & RIDE_LIFECYCLE_CABLE_LIFT)
    {
        ride->lifecycle_flags &= ~RIDE_LIFECYCLE_CABLE_LIFT;
        uint16_t spriteIndex = ride->cable_lift;
        do
        {
            rct_vehicle* vehicle = GET_VEHICLE(spriteIndex);
            invalidate_sprite_2((rct_sprite*)vehicle);
            sprite_remove((rct_sprite*)vehicle);
            spriteIndex = vehicle->next_vehicle_on_train;
        } while (spriteIndex != SPRITE_INDEX_NULL);
    }
}

/**
 *
 *  rct2: 0x006DD506
 */
static void ride_remove_vehicles(Ride* ride)
{
    if (ride->lifecycle_flags & RIDE_LIFECYCLE_ON_TRACK)
    {
        ride->lifecycle_flags &= ~RIDE_LIFECYCLE_ON_TRACK;
        ride->lifecycle_flags &= ~(RIDE_LIFECYCLE_TEST_IN_PROGRESS | RIDE_LIFECYCLE_HAS_STALLED_VEHICLE);

        for (size_t i = 0; i <= MAX_VEHICLES_PER_RIDE; i++)
        {
            uint16_t spriteIndex = ride->vehicles[i];
            while (spriteIndex != SPRITE_INDEX_NULL)
            {
                rct_vehicle* vehicle = GET_VEHICLE(spriteIndex);
                invalidate_sprite_2((rct_sprite*)vehicle);
                sprite_remove((rct_sprite*)vehicle);
                spriteIndex = vehicle->next_vehicle_on_train;
            }

            ride->vehicles[i] = SPRITE_INDEX_NULL;
        }

        for (size_t i = 0; i < MAX_STATIONS; i++)
            ride->stations[i].TrainAtStation = RideStation::NO_TRAIN;
    }
}

/**
 *
 *  rct2: 0x006DD4AC
 */
void ride_clear_for_construction(Ride* ride)
{
    ride->measurement = {};

    ride->lifecycle_flags &= ~(RIDE_LIFECYCLE_BREAKDOWN_PENDING | RIDE_LIFECYCLE_BROKEN_DOWN);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_MAIN | RIDE_INVALIDATE_RIDE_LIST;

    // Open circuit rides will go directly into building mode (creating ghosts) where it would normally clear the stats,
    // however this causes desyncs since it's directly run from the window and other clients would not get it.
    // To prevent these problems, unconditionally invalidate the test results on all clients in multiplayer games.
    if (network_get_mode() != NETWORK_MODE_NONE)
    {
        invalidate_test_results(ride);
    }

    ride_remove_cable_lift(ride);
    ride_remove_vehicles(ride);
    ride_clear_blocked_tiles(ride);

    auto w = window_find_by_number(WC_RIDE, ride->id);
    if (w != nullptr)
        window_event_resize_call(w);
}

/**
 *
 *  rct2: 0x006664DF
 */
void ride_remove_peeps(Ride* ride)
{
    // Find first station
    int8_t stationIndex = ride_get_first_valid_station_start(ride);

    // Get exit position and direction
    int32_t exitX = 0;
    int32_t exitY = 0;
    int32_t exitZ = 0;
    int32_t exitDirection = 255;
    if (stationIndex != -1)
    {
        TileCoordsXYZD location = ride_get_exit_location(ride, stationIndex);
        if (!location.isNull())
        {
            exitX = location.x;
            exitY = location.y;
            exitZ = location.z;
            exitDirection = location.direction;

            exitX = (exitX * 32) - (DirectionOffsets[exitDirection].x * 20) + 16;
            exitY = (exitY * 32) - (DirectionOffsets[exitDirection].y * 20) + 16;
            exitZ = (exitZ * 8) + 2;

            // Reverse direction
            exitDirection = direction_reverse(exitDirection);

            exitDirection *= 8;
        }
    }

    // Place all the peeps at exit
    uint16_t spriteIndex;
    Peep* peep;
    FOR_ALL_PEEPS (spriteIndex, peep)
    {
        if (peep->state == PEEP_STATE_QUEUING_FRONT || peep->state == PEEP_STATE_ENTERING_RIDE
            || peep->state == PEEP_STATE_LEAVING_RIDE || peep->state == PEEP_STATE_ON_RIDE)
        {
            if (peep->current_ride != ride->id)
                continue;

            peep_decrement_num_riders(peep);
            if (peep->state == PEEP_STATE_QUEUING_FRONT && peep->sub_state == PEEP_RIDE_AT_ENTRANCE)
                peep->RemoveFromQueue();

            peep->Invalidate();

            if (exitDirection == 255)
            {
                int32_t x = peep->next_x + 16;
                int32_t y = peep->next_y + 16;
                int32_t z = peep->next_z * 8;
                if (peep->GetNextIsSloped())
                    z += 8;
                z++;
                sprite_move(x, y, z, (rct_sprite*)peep);
            }
            else
            {
                sprite_move(exitX, exitY, exitZ, (rct_sprite*)peep);
                peep->sprite_direction = exitDirection;
            }

            peep->Invalidate();
            peep->state = PEEP_STATE_FALLING;
            peep->SwitchToSpecialSprite(0);

            peep->happiness = std::min(peep->happiness, peep->happiness_target) / 2;
            peep->happiness_target = peep->happiness;
            peep->window_invalidate_flags |= PEEP_INVALIDATE_PEEP_STATS;
        }
    }

    ride->num_riders = 0;
    ride->slide_in_use = 0;
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_MAIN;
}

void ride_clear_blocked_tiles(Ride* ride)
{
    for (int32_t y = 0; y < MAXIMUM_MAP_SIZE_TECHNICAL; y++)
    {
        for (int32_t x = 0; x < MAXIMUM_MAP_SIZE_TECHNICAL; x++)
        {
            auto element = map_get_first_element_at(TileCoordsXY{ x, y }.ToCoordsXY());
            if (element != nullptr)
            {
                do
                {
                    if (element->GetType() == TILE_ELEMENT_TYPE_TRACK && element->AsTrack()->GetRideIndex() == ride->id)
                    {
                        // Unblock footpath element that is at same position
                        auto footpathElement = map_get_footpath_element(
                            TileCoordsXYZ{ x, y, element->base_height }.ToCoordsXYZ());
                        if (footpathElement != nullptr)
                        {
                            footpathElement->AsPath()->SetIsBlockedByVehicle(false);
                        }
                    }
                } while (!(element++)->IsLastForTile());
            }
        }
    }
}

/**
 * Gets the origin track element (sequence 0). Seems to do more than that though and even invalidates track.
 *  rct2: 0x006C683D
 * ax : x
 * bx : direction << 8, type
 * cx : y
 * dx : z
 * si : extra_params
 * di : output_element
 * bp : flags
 */
int32_t sub_6C683D(
    int32_t* x, int32_t* y, int32_t* z, int32_t direction, int32_t type, uint16_t extra_params, TileElement** output_element,
    uint16_t flags)
{
    // Find the relevant track piece, prefer sequence 0 (this ensures correct behaviour for diagonal track pieces)
    auto location = CoordsXYZD{ *x, *y, *z, (Direction)direction };
    auto trackElement = map_get_track_element_at_of_type_seq(location, type, 0);
    if (trackElement == nullptr)
    {
        trackElement = map_get_track_element_at_of_type(location, type);
        if (trackElement == nullptr)
        {
            return 1;
        }
    }

    // Possibly z should be & 0xF8
    auto trackBlock = get_track_def_from_ride_index(trackElement->GetRideIndex(), type);
    if (trackBlock == nullptr)
        return 1;

    // Now find all the elements that belong to this track piece
    int32_t sequence = trackElement->GetSequenceIndex();
    uint8_t mapDirection = trackElement->GetDirection();

    CoordsXY offsets = { trackBlock[sequence].x, trackBlock[sequence].y };
    CoordsXY newCoords = { *x, *y };
    newCoords += offsets.Rotate(direction_reverse(mapDirection));

    *x = newCoords.x;
    *y = newCoords.y;
    *z -= trackBlock[sequence].z;

    int32_t start_x = *x, start_y = *y, start_z = *z;
    *z += trackBlock[0].z;
    for (int32_t i = 0; trackBlock[i].index != 0xFF; ++i)
    {
        CoordsXY cur = { start_x, start_y };
        offsets = { trackBlock[i].x, trackBlock[i].y };
        cur += offsets.Rotate(mapDirection);
        int32_t cur_z = start_z + trackBlock[i].z;

        map_invalidate_tile_full(cur);

        trackElement = map_get_track_element_at_of_type_seq(
            { cur.x, cur.y, cur_z, (Direction)direction }, type, trackBlock[i].index);
        if (trackElement == nullptr)
        {
            return 1;
        }
        if (i == 0 && output_element != nullptr)
        {
            *output_element = (TileElement*)trackElement;
        }
        if (flags & (1 << 0))
        {
            trackElement->SetHighlight(false);
        }
        if (flags & (1 << 1))
        {
            trackElement->SetHighlight(true);
        }
        if (flags & (1 << 2))
        {
            trackElement->SetColourScheme((uint8_t)(extra_params & 0xFF));
        }
        if (flags & (1 << 5))
        {
            trackElement->SetSeatRotation((uint8_t)(extra_params & 0xFF));
        }
        if (flags & (1 << 3))
        {
            trackElement->SetHasCableLift(true);
        }
        if (flags & (1 << 4))
        {
            trackElement->SetHasCableLift(false);
        }
    }
    return 0;
}

void ride_restore_provisional_track_piece()
{
    if (_currentTrackSelectionFlags & TRACK_SELECTION_FLAG_TRACK)
    {
        ride_id_t rideIndex;
        int32_t x, y, z, direction, type, liftHillAndAlternativeState;
        if (window_ride_construction_update_state(
                &type, &direction, &rideIndex, &liftHillAndAlternativeState, &x, &y, &z, nullptr))
        {
            ride_construction_remove_ghosts();
        }
        else
        {
            _currentTrackPrice = place_provisional_track_piece(
                rideIndex, type, direction, liftHillAndAlternativeState, x, y, z);
            window_ride_construction_update_active_elements();
        }
    }
}

void ride_remove_provisional_track_piece()
{
    auto rideIndex = _currentRideIndex;
    auto ride = get_ride(rideIndex);
    if (ride == nullptr || !(_currentTrackSelectionFlags & TRACK_SELECTION_FLAG_TRACK))
    {
        return;
    }

    int32_t x = _unkF440C5.x;
    int32_t y = _unkF440C5.y;
    int32_t z = _unkF440C5.z;
    if (ride->type == RIDE_TYPE_MAZE)
    {
        int32_t flags = GAME_COMMAND_FLAG_APPLY | GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED | GAME_COMMAND_FLAG_NO_SPEND
            | GAME_COMMAND_FLAG_GHOST;
        maze_set_track(x, y, z, flags, false, 0, rideIndex, GC_SET_MAZE_TRACK_FILL);
        maze_set_track(x, y + 16, z, flags, false, 1, rideIndex, GC_SET_MAZE_TRACK_FILL);
        maze_set_track(x + 16, y + 16, z, flags, false, 2, rideIndex, GC_SET_MAZE_TRACK_FILL);
        maze_set_track(x + 16, y, z, flags, false, 3, rideIndex, GC_SET_MAZE_TRACK_FILL);
    }
    else
    {
        int32_t direction = _unkF440C5.direction;
        if (!(direction & 4))
        {
            x -= CoordsDirectionDelta[direction].x;
            y -= CoordsDirectionDelta[direction].y;
        }
        CoordsXYE next_track;
        if (track_block_get_next_from_zero(x, y, z, ride, direction, &next_track, &z, &direction, true))
        {
            auto trackType = next_track.element->AsTrack()->GetTrackType();
            int32_t trackSequence = next_track.element->AsTrack()->GetSequenceIndex();
            auto trackRemoveAction = TrackRemoveAction{ trackType,
                                                        trackSequence,
                                                        { next_track.x, next_track.y, z, static_cast<Direction>(direction) } };
            trackRemoveAction.SetFlags(
                GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED | GAME_COMMAND_FLAG_NO_SPEND | GAME_COMMAND_FLAG_GHOST);
            GameActions::Execute(&trackRemoveAction);
        }
    }
}

/**
 *
 *  rct2: 0x006C96C0
 */
void ride_construction_remove_ghosts()
{
    if (_currentTrackSelectionFlags & TRACK_SELECTION_FLAG_ENTRANCE_OR_EXIT)
    {
        ride_entrance_exit_remove_ghost();
        _currentTrackSelectionFlags &= ~TRACK_SELECTION_FLAG_ENTRANCE_OR_EXIT;
    }
    if (_currentTrackSelectionFlags & TRACK_SELECTION_FLAG_TRACK)
    {
        ride_remove_provisional_track_piece();
        _currentTrackSelectionFlags &= ~TRACK_SELECTION_FLAG_TRACK;
    }
}

/*
 *  rct2: 0x006C9627
 */
void ride_construction_invalidate_current_track()
{
    int32_t x, y, z;

    switch (_rideConstructionState)
    {
        case RIDE_CONSTRUCTION_STATE_SELECTED:
            x = _currentTrackBegin.x;
            y = _currentTrackBegin.y;
            z = _currentTrackBegin.z;
            sub_6C683D(&x, &y, &z, _currentTrackPieceDirection & 3, _currentTrackPieceType, 0, nullptr, 1);
            break;
        case RIDE_CONSTRUCTION_STATE_MAZE_BUILD:
        case RIDE_CONSTRUCTION_STATE_MAZE_MOVE:
        case RIDE_CONSTRUCTION_STATE_MAZE_FILL:
            if (_currentTrackSelectionFlags & TRACK_SELECTION_FLAG_ARROW)
            {
                map_invalidate_tile_full(_currentTrackBegin.ToTileStart());
                gMapSelectFlags &= ~MAP_SELECT_FLAG_ENABLE_ARROW;
            }
            break;
        default:
            if (_currentTrackSelectionFlags & TRACK_SELECTION_FLAG_ARROW)
            {
                _currentTrackSelectionFlags &= ~TRACK_SELECTION_FLAG_ARROW;
                gMapSelectFlags &= ~MAP_SELECT_FLAG_ENABLE_ARROW;
                map_invalidate_tile_full(_currentTrackBegin);
            }
            ride_construction_remove_ghosts();
            break;
    }
}

/**
 *
 *  rct2: 0x006C9B19
 */
static void ride_construction_reset_current_piece()
{
    auto ride = get_ride(_currentRideIndex);
    if (ride == nullptr)
        return;

    if (!ride_type_has_flag(ride->type, RIDE_TYPE_FLAG_HAS_NO_TRACK) || ride->num_stations == 0)
    {
        _currentTrackCurve = RideConstructionDefaultTrackType[ride->type] | 0x100;
        _currentTrackSlopeEnd = 0;
        _currentTrackBankEnd = 0;
        _currentTrackLiftHill = 0;
        _currentTrackAlternative = RIDE_TYPE_NO_ALTERNATIVES;
        if (RideData4[ride->type].flags & RIDE_TYPE_FLAG4_START_CONSTRUCTION_INVERTED)
        {
            _currentTrackAlternative |= RIDE_TYPE_ALTERNATIVE_TRACK_TYPE;
        }
        _previousTrackSlopeEnd = 0;
        _previousTrackBankEnd = 0;
    }
    else
    {
        _currentTrackCurve = 0xFFFF;
        _rideConstructionState = RIDE_CONSTRUCTION_STATE_0;
    }
}

/**
 *
 *  rct2: 0x006C9800
 */
void ride_construction_set_default_next_piece()
{
    auto rideIndex = _currentRideIndex;
    auto ride = get_ride(rideIndex);
    if (ride == nullptr)
        return;

    int32_t x, y, z, direction, trackType, curve, bank, slope;
    track_begin_end trackBeginEnd;
    CoordsXYE xyElement;
    TileElement* tileElement;
    _currentTrackPrice = MONEY32_UNDEFINED;
    switch (_rideConstructionState)
    {
        case RIDE_CONSTRUCTION_STATE_FRONT:
            x = _currentTrackBegin.x;
            y = _currentTrackBegin.y;
            z = _currentTrackBegin.z;
            direction = _currentTrackPieceDirection;
            if (!track_block_get_previous_from_zero(x, y, z, ride, direction, &trackBeginEnd))
            {
                ride_construction_reset_current_piece();
                return;
            }
            tileElement = trackBeginEnd.begin_element;
            trackType = tileElement->AsTrack()->GetTrackType();

            if (ride_type_has_flag(ride->type, RIDE_TYPE_FLAG_HAS_NO_TRACK))
            {
                ride_construction_reset_current_piece();
                return;
            }

            // Set whether track is covered
            _currentTrackAlternative &= ~RIDE_TYPE_ALTERNATIVE_TRACK_TYPE;
            if (RideData4[ride->type].flags & RIDE_TYPE_FLAG4_HAS_ALTERNATIVE_TRACK_TYPE)
            {
                if (tileElement->AsTrack()->IsInverted())
                {
                    _currentTrackAlternative |= RIDE_TYPE_ALTERNATIVE_TRACK_TYPE;
                }
            }

            if (ride_type_has_flag(ride->type, RIDE_TYPE_FLAG_FLAT_RIDE))
            {
                curve = gFlatRideTrackCurveChain[trackType].next;
                bank = FlatRideTrackDefinitions[trackType].bank_end;
                slope = FlatRideTrackDefinitions[trackType].vangle_end;
            }
            else
            {
                if (track_element_is_booster(ride->type, trackType))
                {
                    curve = 0x100 | TRACK_ELEM_BOOSTER;
                }
                else
                {
                    curve = gTrackCurveChain[trackType].next;
                }
                bank = TrackDefinitions[trackType].bank_end;
                slope = TrackDefinitions[trackType].vangle_end;
            }

            // Set track curve
            _currentTrackCurve = curve;

            // Set track banking
            if (RideData4[ride->type].flags & RIDE_TYPE_FLAG4_HAS_ALTERNATIVE_TRACK_TYPE)
            {
                if (bank == TRACK_BANK_UPSIDE_DOWN)
                {
                    bank = TRACK_BANK_NONE;
                    _currentTrackAlternative ^= RIDE_TYPE_ALTERNATIVE_TRACK_TYPE;
                }
            }
            _currentTrackBankEnd = bank;
            _previousTrackBankEnd = bank;

            // Set track slope and lift hill
            _currentTrackSlopeEnd = slope;
            _previousTrackSlopeEnd = slope;
            _currentTrackLiftHill = tileElement->AsTrack()->HasChain() && slope != TRACK_SLOPE_DOWN_25
                && slope != TRACK_SLOPE_DOWN_60;
            break;
        case RIDE_CONSTRUCTION_STATE_BACK:
            x = _currentTrackBegin.x;
            y = _currentTrackBegin.y;
            z = _currentTrackBegin.z;
            direction = direction_reverse(_currentTrackPieceDirection);
            if (!track_block_get_next_from_zero(x, y, z, ride, direction, &xyElement, &z, &direction, false))
            {
                ride_construction_reset_current_piece();
                return;
            }
            tileElement = xyElement.element;
            trackType = tileElement->AsTrack()->GetTrackType();

            // Set whether track is covered
            _currentTrackAlternative &= ~RIDE_TYPE_ALTERNATIVE_TRACK_TYPE;
            if (RideData4[ride->type].flags & RIDE_TYPE_FLAG4_HAS_ALTERNATIVE_TRACK_TYPE)
            {
                if (tileElement->AsTrack()->IsInverted())
                {
                    _currentTrackAlternative |= RIDE_TYPE_ALTERNATIVE_TRACK_TYPE;
                }
            }

            if (ride_type_has_flag(ride->type, RIDE_TYPE_FLAG_FLAT_RIDE))
            {
                curve = gFlatRideTrackCurveChain[trackType].previous;
                bank = FlatRideTrackDefinitions[trackType].bank_start;
                slope = FlatRideTrackDefinitions[trackType].vangle_start;
            }
            else
            {
                if (track_element_is_booster(ride->type, trackType))
                {
                    curve = 0x100 | TRACK_ELEM_BOOSTER;
                }
                else
                {
                    curve = gTrackCurveChain[trackType].previous;
                }
                bank = TrackDefinitions[trackType].bank_start;
                slope = TrackDefinitions[trackType].vangle_start;
            }

            // Set track curve
            _currentTrackCurve = curve;

            // Set track banking
            if (RideData4[ride->type].flags & RIDE_TYPE_FLAG4_HAS_ALTERNATIVE_TRACK_TYPE)
            {
                if (bank == TRACK_BANK_UPSIDE_DOWN)
                {
                    bank = TRACK_BANK_NONE;
                    _currentTrackAlternative ^= RIDE_TYPE_ALTERNATIVE_TRACK_TYPE;
                }
            }
            _currentTrackBankEnd = bank;
            _previousTrackBankEnd = bank;

            // Set track slope and lift hill
            _currentTrackSlopeEnd = slope;
            _previousTrackSlopeEnd = slope;
            if (!gCheatsEnableChainLiftOnAllTrack)
            {
                _currentTrackLiftHill = tileElement->AsTrack()->HasChain();
            }
            break;
    }
}

/**
 *
 *  rct2: 0x006C9296
 */
void ride_select_next_section()
{
    if (_rideConstructionState == RIDE_CONSTRUCTION_STATE_SELECTED)
    {
        ride_construction_invalidate_current_track();
        int32_t x = _currentTrackBegin.x;
        int32_t y = _currentTrackBegin.y;
        int32_t z = _currentTrackBegin.z;
        int32_t direction = _currentTrackPieceDirection;
        int32_t type = _currentTrackPieceType;
        TileElement* tileElement;
        if (sub_6C683D(&x, &y, &z, direction & 3, type, 0, &tileElement, 0))
        {
            _rideConstructionState = RIDE_CONSTRUCTION_STATE_0;
            window_ride_construction_update_active_elements();
            return;
        }

        // Invalidate previous track piece (we may not be changing height!)
        virtual_floor_invalidate();

        CoordsXYE inputElement, outputElement;
        inputElement.x = x;
        inputElement.y = y;
        inputElement.element = tileElement;
        if (track_block_get_next(&inputElement, &outputElement, &z, &direction))
        {
            x = outputElement.x;
            y = outputElement.y;
            tileElement = outputElement.element;
            if (!scenery_tool_is_active())
            {
                // Set next element's height.
                virtual_floor_set_height(tileElement->GetBaseZ());
            }
        }
        else
        {
            _rideConstructionState = RIDE_CONSTRUCTION_STATE_FRONT;
            _currentTrackBegin.x = outputElement.x;
            _currentTrackBegin.y = outputElement.y;
            _currentTrackBegin.z = z;
            _currentTrackPieceDirection = direction;
            _currentTrackPieceType = tileElement->AsTrack()->GetTrackType();
            _currentTrackSelectionFlags = 0;
            _rideConstructionArrowPulseTime = 0;
            ride_construction_set_default_next_piece();
            window_ride_construction_update_active_elements();
            return;
        }

        _currentTrackBegin.x = x;
        _currentTrackBegin.y = y;
        _currentTrackBegin.z = z;
        _currentTrackPieceDirection = tileElement->GetDirection();
        _currentTrackPieceType = tileElement->AsTrack()->GetTrackType();
        _currentTrackSelectionFlags = 0;
        _rideConstructionArrowPulseTime = 0;
        window_ride_construction_update_active_elements();
    }
    else if (_rideConstructionState == RIDE_CONSTRUCTION_STATE_BACK)
    {
        if (ride_select_forwards_from_back())
        {
            window_ride_construction_update_active_elements();
        }
    }
}

/**
 *
 *  rct2: 0x006C93B8
 */
void ride_select_previous_section()
{
    if (_rideConstructionState == RIDE_CONSTRUCTION_STATE_SELECTED)
    {
        ride_construction_invalidate_current_track();
        int32_t x = _currentTrackBegin.x;
        int32_t y = _currentTrackBegin.y;
        int32_t z = _currentTrackBegin.z;
        int32_t direction = _currentTrackPieceDirection;
        int32_t type = _currentTrackPieceType;
        TileElement* tileElement;
        if (sub_6C683D(&x, &y, &z, direction & 3, type, 0, &tileElement, 0))
        {
            _rideConstructionState = RIDE_CONSTRUCTION_STATE_0;
            window_ride_construction_update_active_elements();
            return;
        }

        // Invalidate previous track piece (we may not be changing height!)
        virtual_floor_invalidate();

        track_begin_end trackBeginEnd;
        if (track_block_get_previous(x, y, tileElement, &trackBeginEnd))
        {
            _currentTrackBegin.x = trackBeginEnd.begin_x;
            _currentTrackBegin.y = trackBeginEnd.begin_y;
            _currentTrackBegin.z = trackBeginEnd.begin_z;
            _currentTrackPieceDirection = trackBeginEnd.begin_direction;
            _currentTrackPieceType = trackBeginEnd.begin_element->AsTrack()->GetTrackType();
            _currentTrackSelectionFlags = 0;
            _rideConstructionArrowPulseTime = 0;
            if (!scenery_tool_is_active())
            {
                // Set previous element's height.
                virtual_floor_set_height(trackBeginEnd.begin_element->GetBaseZ());
            }
            window_ride_construction_update_active_elements();
        }
        else
        {
            _rideConstructionState = RIDE_CONSTRUCTION_STATE_BACK;
            _currentTrackBegin.x = trackBeginEnd.end_x;
            _currentTrackBegin.y = trackBeginEnd.end_y;
            _currentTrackBegin.z = trackBeginEnd.begin_z;
            _currentTrackPieceDirection = trackBeginEnd.end_direction;
            _currentTrackPieceType = tileElement->AsTrack()->GetTrackType();
            _currentTrackSelectionFlags = 0;
            _rideConstructionArrowPulseTime = 0;
            ride_construction_set_default_next_piece();
            window_ride_construction_update_active_elements();
        }
    }
    else if (_rideConstructionState == RIDE_CONSTRUCTION_STATE_FRONT)
    {
        if (ride_select_backwards_from_front())
        {
            window_ride_construction_update_active_elements();
        }
    }
}

/**
 *
 *  rct2: 0x006CC2CA
 */
static bool ride_modify_entrance_or_exit(const CoordsXYE& tileElement)
{
    if (tileElement.element == nullptr)
        return false;

    auto entranceElement = tileElement.element->AsEntrance();
    if (entranceElement == nullptr)
        return false;

    auto rideIndex = entranceElement->GetRideIndex();
    auto ride = get_ride(rideIndex);
    if (ride == nullptr)
        return false;

    auto entranceType = entranceElement->GetEntranceType();
    if (entranceType != ENTRANCE_TYPE_RIDE_ENTRANCE && entranceType != ENTRANCE_TYPE_RIDE_EXIT)
        return false;

    int32_t stationIndex = entranceElement->GetStationIndex();

    // Get or create construction window for ride
    auto constructionWindow = window_find_by_class(WC_RIDE_CONSTRUCTION);
    if (constructionWindow == nullptr)
    {
        if (!ride_initialise_construction_window(ride))
            return false;

        constructionWindow = window_find_by_class(WC_RIDE_CONSTRUCTION);
        if (constructionWindow == nullptr)
            return false;
    }

    ride_construction_invalidate_current_track();
    if (_rideConstructionState != RIDE_CONSTRUCTION_STATE_ENTRANCE_EXIT || !(input_test_flag(INPUT_FLAG_TOOL_ACTIVE))
        || gCurrentToolWidget.window_classification != WC_RIDE_CONSTRUCTION)
    {
        // Replace entrance / exit
        tool_set(
            constructionWindow,
            entranceType == ENTRANCE_TYPE_RIDE_ENTRANCE ? WC_RIDE_CONSTRUCTION__WIDX_ENTRANCE : WC_RIDE_CONSTRUCTION__WIDX_EXIT,
            TOOL_CROSSHAIR);
        gRideEntranceExitPlaceType = entranceType;
        gRideEntranceExitPlaceRideIndex = rideIndex;
        gRideEntranceExitPlaceStationIndex = stationIndex;
        input_set_flag(INPUT_FLAG_6, true);
        if (_rideConstructionState != RIDE_CONSTRUCTION_STATE_ENTRANCE_EXIT)
        {
            gRideEntranceExitPlacePreviousRideConstructionState = _rideConstructionState;
            _rideConstructionState = RIDE_CONSTRUCTION_STATE_ENTRANCE_EXIT;
        }

        window_ride_construction_update_active_elements();
        gMapSelectFlags &= ~MAP_SELECT_FLAG_ENABLE_CONSTRUCT;
    }
    else
    {
        // Remove entrance / exit
        auto rideEntranceExitRemove = RideEntranceExitRemoveAction(
            { tileElement.x, tileElement.y }, rideIndex, stationIndex, entranceType == ENTRANCE_TYPE_RIDE_EXIT);

        rideEntranceExitRemove.SetCallback([=](const GameAction* ga, const GameActionResult* result) {
            gCurrentToolWidget.widget_index = entranceType == ENTRANCE_TYPE_RIDE_ENTRANCE ? WC_RIDE_CONSTRUCTION__WIDX_ENTRANCE
                                                                                          : WC_RIDE_CONSTRUCTION__WIDX_EXIT;
            gRideEntranceExitPlaceType = entranceType;
            window_invalidate_by_class(WC_RIDE_CONSTRUCTION);
        });

        GameActions::Execute(&rideEntranceExitRemove);
    }

    window_invalidate_by_class(WC_RIDE_CONSTRUCTION);
    return true;
}

/**
 *
 *  rct2: 0x006CC287
 */
static bool ride_modify_maze(const CoordsXYE& tileElement)
{
    if (tileElement.element != nullptr)
    {
        auto trackElement = tileElement.element->AsTrack();
        if (trackElement != nullptr)
        {
            _currentRideIndex = trackElement->GetRideIndex();
            _rideConstructionState = RIDE_CONSTRUCTION_STATE_MAZE_BUILD;
            _currentTrackBegin.x = tileElement.x;
            _currentTrackBegin.y = tileElement.y;
            _currentTrackBegin.z = trackElement->GetBaseZ();
            _currentTrackSelectionFlags = 0;
            _rideConstructionArrowPulseTime = 0;

            auto intent = Intent(INTENT_ACTION_UPDATE_MAZE_CONSTRUCTION);
            context_broadcast_intent(&intent);
            return true;
        }
    }
    return false;
}

/**
 *
 *  rct2: 0x006CC056
 */
bool ride_modify(CoordsXYE* input)
{
    auto tileElement = *input;
    if (tileElement.element == nullptr)
        return false;

    auto rideIndex = tile_element_get_ride_index(tileElement.element);
    auto ride = get_ride(rideIndex);
    if (ride == nullptr)
    {
        return false;
    }

    auto rideEntry = ride->GetRideEntry();
    if (rideEntry == nullptr || !ride_check_if_construction_allowed(ride))
        return false;

    if (ride->lifecycle_flags & RIDE_LIFECYCLE_INDESTRUCTIBLE)
    {
        ride->FormatNameTo(gCommonFormatArgs + 6);
        context_show_error(
            STR_CANT_START_CONSTRUCTION_ON, STR_LOCAL_AUTHORITY_FORBIDS_DEMOLITION_OR_MODIFICATIONS_TO_THIS_RIDE);
        return false;
    }

    // Stop the ride again to clear all vehicles and peeps (compatible with network games)
    if (ride->status != RIDE_STATUS_SIMULATING)
    {
        ride_set_status(ride, RIDE_STATUS_CLOSED);
    }

    // Check if element is a station entrance or exit
    if (tileElement.element->GetType() == TILE_ELEMENT_TYPE_ENTRANCE)
        return ride_modify_entrance_or_exit(tileElement);

    ride_create_or_find_construction_window(rideIndex);

    if (ride->type == RIDE_TYPE_MAZE)
    {
        return ride_modify_maze(tileElement);
    }

    if (ride_type_has_flag(ride->type, RIDE_TYPE_FLAG_CANNOT_HAVE_GAPS))
    {
        CoordsXYE endOfTrackElement{};
        if (ride_find_track_gap(ride, &tileElement, &endOfTrackElement))
            tileElement = endOfTrackElement;
    }

    if (tileElement.element == nullptr || tileElement.element->GetType() != TILE_ELEMENT_TYPE_TRACK)
        return false;

    int32_t x = tileElement.x;
    int32_t y = tileElement.y;
    int32_t z = tileElement.element->GetBaseZ();
    int32_t direction = tileElement.element->GetDirection();
    int32_t type = tileElement.element->AsTrack()->GetTrackType();

    if (sub_6C683D(&x, &y, &z, direction, type, 0, nullptr, 0))
        return false;

    _currentRideIndex = rideIndex;
    _rideConstructionState = RIDE_CONSTRUCTION_STATE_SELECTED;
    _currentTrackBegin.x = x;
    _currentTrackBegin.y = y;
    _currentTrackBegin.z = z;
    _currentTrackPieceDirection = direction;
    _currentTrackPieceType = type;
    _currentTrackSelectionFlags = 0;
    _rideConstructionArrowPulseTime = 0;

    if (ride_type_has_flag(ride->type, RIDE_TYPE_FLAG_HAS_NO_TRACK))
    {
        window_ride_construction_update_active_elements();
        return true;
    }

    ride_select_next_section();
    if (_rideConstructionState == RIDE_CONSTRUCTION_STATE_FRONT)
    {
        window_ride_construction_update_active_elements();
        return true;
    }

    _rideConstructionState = RIDE_CONSTRUCTION_STATE_SELECTED;
    _currentTrackBegin.x = x;
    _currentTrackBegin.y = y;
    _currentTrackBegin.z = z;
    _currentTrackPieceDirection = direction;
    _currentTrackPieceType = type;
    _currentTrackSelectionFlags = 0;
    _rideConstructionArrowPulseTime = 0;

    ride_select_previous_section();

    if (_rideConstructionState != RIDE_CONSTRUCTION_STATE_BACK)
    {
        _rideConstructionState = RIDE_CONSTRUCTION_STATE_SELECTED;
        _currentTrackBegin.x = x;
        _currentTrackBegin.y = y;
        _currentTrackBegin.z = z;
        _currentTrackPieceDirection = direction;
        _currentTrackPieceType = type;
        _currentTrackSelectionFlags = 0;
        _rideConstructionArrowPulseTime = 0;
    }

    window_ride_construction_update_active_elements();
    return true;
}

/**
 *
 *  rct2: 0x006CC3FB
 */
int32_t ride_initialise_construction_window(Ride* ride)
{
    rct_window* w;

    tool_cancel();

    if (!ride_check_if_construction_allowed(ride))
        return 0;

    ride_clear_for_construction(ride);
    ride_remove_peeps(ride);

    w = ride_create_or_find_construction_window(ride->id);

    tool_set(w, WC_RIDE_CONSTRUCTION__WIDX_CONSTRUCT, TOOL_CROSSHAIR);
    input_set_flag(INPUT_FLAG_6, true);

    ride = get_ride(_currentRideIndex);
    if (ride == nullptr)
        return 0;

    _currentTrackCurve = RideConstructionDefaultTrackType[ride->type] | 0x100;
    _currentTrackSlopeEnd = 0;
    _currentTrackBankEnd = 0;
    _currentTrackLiftHill = 0;
    _currentTrackAlternative = RIDE_TYPE_NO_ALTERNATIVES;

    if (RideData4[ride->type].flags & RIDE_TYPE_FLAG4_START_CONSTRUCTION_INVERTED)
        _currentTrackAlternative |= RIDE_TYPE_ALTERNATIVE_TRACK_TYPE;

    _previousTrackBankEnd = 0;
    _previousTrackSlopeEnd = 0;

    _currentTrackPieceDirection = 0;
    _rideConstructionState = RIDE_CONSTRUCTION_STATE_PLACE;
    _currentTrackSelectionFlags = 0;
    _rideConstructionArrowPulseTime = 0;

    window_ride_construction_update_active_elements();
    return 1;
}

#pragma endregion

#pragma region Update functions

/**
 *
 *  rct2: 0x006ABE4C
 */
void Ride::UpdateAll()
{
    // Remove all rides if scenario editor
    if (gScreenFlags & SCREEN_FLAGS_SCENARIO_EDITOR)
    {
        if (gS6Info.editor_step <= EDITOR_STEP_INVENTIONS_LIST_SET_UP)
            for (auto& ride : GetRideManager())
                ride.Delete();
        return;
    }

    window_update_viewport_ride_music();

    // Update rides
    for (auto& ride : GetRideManager())
        ride.Update();

    ride_music_update_final();
}

std::unique_ptr<TrackDesign> Ride::SaveToTrackDesign() const
{
    if (!(lifecycle_flags & RIDE_LIFECYCLE_TESTED))
    {
        context_show_error(STR_CANT_SAVE_TRACK_DESIGN, STR_NONE);
        return nullptr;
    }

    if (!ride_has_ratings(this))
    {
        context_show_error(STR_CANT_SAVE_TRACK_DESIGN, STR_NONE);
        return nullptr;
    }

    auto td = std::make_unique<TrackDesign>();
    auto errMessage = td->CreateTrackDesign(*this);
    if (errMessage != STR_NONE)
    {
        context_show_error(STR_CANT_SAVE_TRACK_DESIGN, errMessage);
        return nullptr;
    }

    return td;
}

/**
 *
 *  rct2: 0x006ABE73
 */
void Ride::Update()
{
    if (vehicle_change_timeout != 0)
        vehicle_change_timeout--;

    ride_music_update(this);

    // Update stations
    if (type != RIDE_TYPE_MAZE)
        for (int32_t i = 0; i < MAX_STATIONS; i++)
            ride_update_station(this, i);

    // Update financial statistics
    num_customers_timeout++;

    if (num_customers_timeout >= 960)
    {
        // This is meant to update about every 30 seconds
        num_customers_timeout = 0;

        // Shift number of customers history, start of the array is the most recent one
        for (int32_t i = CUSTOMER_HISTORY_SIZE - 1; i > 0; i--)
        {
            num_customers[i] = num_customers[i - 1];
        }
        num_customers[0] = cur_num_customers;

        cur_num_customers = 0;
        window_invalidate_flags |= RIDE_INVALIDATE_RIDE_CUSTOMER;

        income_per_hour = CalculateIncomePerHour();
        window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

        if (upkeep_cost != MONEY16_UNDEFINED)
            profit = (income_per_hour - ((money32)upkeep_cost * 16));
    }

    // Ride specific updates
    if (type == RIDE_TYPE_CHAIRLIFT)
        UpdateChairlift();
    else if (type == RIDE_TYPE_SPIRAL_SLIDE)
        UpdateSpiralSlide();

    ride_breakdown_update(this);

    // Various things include news messages
    if (lifecycle_flags & (RIDE_LIFECYCLE_BREAKDOWN_PENDING | RIDE_LIFECYCLE_BROKEN_DOWN | RIDE_LIFECYCLE_DUE_INSPECTION))
        if (((gCurrentTicks >> 1) & 255) == (uint32_t)id)
            ride_breakdown_status_update(this);

    ride_inspection_update(this);

    // If ride is simulating but crashed, reset the vehicles
    if (status == RIDE_STATUS_SIMULATING && (lifecycle_flags & RIDE_LIFECYCLE_CRASHED))
    {
        // We require this to execute right away during the simulation, always ignore network and queue.
        auto gameAction = RideSetStatusAction(id, RIDE_STATUS_SIMULATING);
        GameActions::ExecuteNested(&gameAction);
    }
}

/**
 *
 *  rct2: 0x006AC489
 */
void Ride::UpdateChairlift()
{
    if (!(lifecycle_flags & RIDE_LIFECYCLE_ON_TRACK))
        return;
    if ((lifecycle_flags & (RIDE_LIFECYCLE_BREAKDOWN_PENDING | RIDE_LIFECYCLE_BROKEN_DOWN | RIDE_LIFECYCLE_CRASHED))
        && breakdown_reason_pending == 0)
        return;

    uint16_t old_chairlift_bullwheel_rotation = chairlift_bullwheel_rotation >> 14;
    chairlift_bullwheel_rotation += speed * 2048;
    if (old_chairlift_bullwheel_rotation == speed / 8)
        return;

    auto bullwheelLoc = ChairliftBullwheelLocation[0].ToCoordsXYZ();
    map_invalidate_tile_zoom1({ bullwheelLoc, bullwheelLoc.z, bullwheelLoc.z + (4 * COORDS_Z_STEP) });

    bullwheelLoc = ChairliftBullwheelLocation[1].ToCoordsXYZ();
    map_invalidate_tile_zoom1({ bullwheelLoc, bullwheelLoc.z, bullwheelLoc.z + (4 * COORDS_Z_STEP) });
}

/**
 *
 *  rct2: 0x0069A3A2
 * edi: ride (in code as bytes offset from start of rides list)
 * bl: happiness
 */
void ride_update_satisfaction(Ride* ride, uint8_t happiness)
{
    ride->satisfaction_next += happiness;
    ride->satisfaction_time_out++;
    if (ride->satisfaction_time_out >= 20)
    {
        ride->satisfaction = ride->satisfaction_next >> 2;
        ride->satisfaction_next = 0;
        ride->satisfaction_time_out = 0;
        ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_CUSTOMER;
    }
}

/**
 *
 *  rct2: 0x0069A3D7
 * Updates the ride popularity
 * edi : ride
 * bl  : pop_amount
 * pop_amount can be zero if peep visited but did not purchase.
 */
void ride_update_popularity(Ride* ride, uint8_t pop_amount)
{
    ride->popularity_next += pop_amount;
    ride->popularity_time_out++;
    if (ride->popularity_time_out < 25)
        return;

    ride->popularity = ride->popularity_next;
    ride->popularity_next = 0;
    ride->popularity_time_out = 0;
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_CUSTOMER;
}

/** rct2: 0x0098DDB8, 0x0098DDBA */
static constexpr const CoordsXY ride_spiral_slide_main_tile_offset[][4] = {
    {
        { 32, 32 },
        { 0, 32 },
        { 0, 0 },
        { 32, 0 },
    },
    {
        { 32, 0 },
        { 0, 0 },
        { 0, -32 },
        { 32, -32 },
    },
    {
        { 0, 0 },
        { -32, 0 },
        { -32, -32 },
        { 0, -32 },
    },
    {
        { 0, 0 },
        { 0, 32 },
        { -32, 32 },
        { -32, 0 },
    },
};

/**
 *
 *  rct2: 0x006AC545
 */
void Ride::UpdateSpiralSlide()
{
    if (gCurrentTicks & 3)
        return;
    if (slide_in_use == 0)
        return;

    spiral_slide_progress++;
    if (spiral_slide_progress >= 48)
    {
        slide_in_use--;

        Peep* peep = GET_PEEP(slide_peep);
        peep->destination_x++;
    }

    const uint8_t current_rotation = get_current_rotation();
    // Invalidate something related to station start
    for (int32_t i = 0; i < MAX_STATIONS; i++)
    {
        if (stations[i].Start.isNull())
            continue;

        int32_t x = stations[i].Start.x;
        int32_t y = stations[i].Start.y;

        TileElement* tileElement = ride_get_station_start_track_element(this, i);
        if (tileElement == nullptr)
            continue;

        int32_t rotation = tileElement->GetDirection();
        x *= 32;
        y *= 32;
        x += ride_spiral_slide_main_tile_offset[rotation][current_rotation].x;
        y += ride_spiral_slide_main_tile_offset[rotation][current_rotation].y;

        map_invalidate_tile_zoom0({ x, y, tileElement->GetBaseZ(), tileElement->GetClearanceZ() });
    }
}

#pragma endregion

#pragma region Breakdown and inspection functions

static uint8_t _breakdownProblemProbabilities[] = {
    25, // BREAKDOWN_SAFETY_CUT_OUT
    12, // BREAKDOWN_RESTRAINTS_STUCK_CLOSED
    10, // BREAKDOWN_RESTRAINTS_STUCK_OPEN
    13, // BREAKDOWN_DOORS_STUCK_CLOSED
    10, // BREAKDOWN_DOORS_STUCK_OPEN
    6,  // BREAKDOWN_VEHICLE_MALFUNCTION
    0,  // BREAKDOWN_BRAKES_FAILURE
    3   // BREAKDOWN_CONTROL_FAILURE
};

/**
 *
 *  rct2: 0x006AC7C2
 */
static void ride_inspection_update(Ride* ride)
{
    if (gCurrentTicks & 2047)
        return;
    if (gScreenFlags & SCREEN_FLAGS_TRACK_DESIGNER)
        return;

    ride->last_inspection++;
    if (ride->last_inspection == 0)
        ride->last_inspection--;

    int32_t inspectionIntervalMinutes = RideInspectionInterval[ride->inspection_interval];
    // An inspection interval of 0 minutes means the ride is set to never be inspected.
    if (inspectionIntervalMinutes == 0)
    {
        ride->lifecycle_flags &= ~RIDE_LIFECYCLE_DUE_INSPECTION;
        return;
    }

    if (RideAvailableBreakdowns[ride->type] == 0)
        return;

    if (inspectionIntervalMinutes > ride->last_inspection)
        return;

    if (ride->lifecycle_flags
        & (RIDE_LIFECYCLE_BREAKDOWN_PENDING | RIDE_LIFECYCLE_BROKEN_DOWN | RIDE_LIFECYCLE_DUE_INSPECTION
           | RIDE_LIFECYCLE_CRASHED))
        return;

    // Inspect the first station that has an exit
    ride->lifecycle_flags |= RIDE_LIFECYCLE_DUE_INSPECTION;
    ride->mechanic_status = RIDE_MECHANIC_STATUS_CALLING;

    int8_t stationIndex = ride_get_first_valid_station_exit(ride);
    ride->inspection_station = (stationIndex != -1) ? stationIndex : 0;
}

static int32_t get_age_penalty(Ride* ride)
{
    int32_t years;
    years = date_get_year(gDateMonthsElapsed - ride->build_date);
    switch (years)
    {
        case 0:
            return 0;
        case 1:
            return ride->unreliability_factor / 8;
        case 2:
            return ride->unreliability_factor / 4;
        case 3:
        case 4:
            return ride->unreliability_factor / 2;
        case 5:
        case 6:
        case 7:
            return ride->unreliability_factor;
        default:
            return ride->unreliability_factor * 2;
    }
}

/**
 *
 *  rct2: 0x006AC622
 */
static void ride_breakdown_update(Ride* ride)
{
    if (gCurrentTicks & 255)
        return;
    if (gScreenFlags & SCREEN_FLAGS_TRACK_DESIGNER)
        return;

    if (ride->lifecycle_flags & (RIDE_LIFECYCLE_BROKEN_DOWN | RIDE_LIFECYCLE_CRASHED))
        ride->downtime_history[0]++;

    if (!(gCurrentTicks & 8191))
    {
        int32_t totalDowntime = 0;

        for (int32_t i = 0; i < DOWNTIME_HISTORY_SIZE; i++)
        {
            totalDowntime += ride->downtime_history[i];
        }

        ride->downtime = std::min(totalDowntime / 2, 100);

        for (int32_t i = DOWNTIME_HISTORY_SIZE - 1; i > 0; i--)
        {
            ride->downtime_history[i] = ride->downtime_history[i - 1];
        }
        ride->downtime_history[0] = 0;

        ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_MAINTENANCE;
    }

    if (ride->lifecycle_flags & (RIDE_LIFECYCLE_BREAKDOWN_PENDING | RIDE_LIFECYCLE_BROKEN_DOWN | RIDE_LIFECYCLE_CRASHED))
        return;
    if (ride->status == RIDE_STATUS_CLOSED || ride->status == RIDE_STATUS_SIMULATING)
        return;

    if (!ride->CanBreakDown())
    {
        ride->reliability = RIDE_INITIAL_RELIABILITY;
        return;
    }

    // Calculate breakdown probability?
    int32_t unreliabilityAccumulator = ride->unreliability_factor + get_age_penalty(ride);
    ride->reliability = (uint16_t)std::max(0, (ride->reliability - unreliabilityAccumulator));
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_MAINTENANCE;

    // Random probability of a breakdown. Roughly this is 1 in
    //
    // (25000 - reliability) / 3 000 000
    //
    // a 0.8% chance, less the breakdown factor which accumulates as the game
    // continues.
    if ((ride->reliability == 0 || (int32_t)(scenario_rand() & 0x2FFFFF) <= 1 + RIDE_INITIAL_RELIABILITY - ride->reliability)
        && !gCheatsDisableAllBreakdowns)
    {
        int32_t breakdownReason = ride_get_new_breakdown_problem(ride);
        if (breakdownReason != -1)
            ride_prepare_breakdown(ride, breakdownReason);
    }
}

/**
 *
 *  rct2: 0x006B7294
 */
static int32_t ride_get_new_breakdown_problem(Ride* ride)
{
    int32_t availableBreakdownProblems, monthsOld, totalProbability, randomProbability, problemBits, breakdownProblem;

    // Brake failure is more likely when it's raining
    _breakdownProblemProbabilities[BREAKDOWN_BRAKES_FAILURE] = climate_is_raining() ? 20 : 3;

    if (!ride->CanBreakDown())
        return -1;

    availableBreakdownProblems = RideAvailableBreakdowns[ride->type];

    // Calculate the total probability range for all possible breakdown problems
    totalProbability = 0;
    problemBits = availableBreakdownProblems;
    while (problemBits != 0)
    {
        breakdownProblem = bitscanforward(problemBits);
        problemBits &= ~(1 << breakdownProblem);
        totalProbability += _breakdownProblemProbabilities[breakdownProblem];
    }
    if (totalProbability == 0)
        return -1;

    // Choose a random number within this range
    randomProbability = scenario_rand() % totalProbability;

    // Find which problem range the random number lies
    problemBits = availableBreakdownProblems;
    do
    {
        breakdownProblem = bitscanforward(problemBits);
        problemBits &= ~(1 << breakdownProblem);
        randomProbability -= _breakdownProblemProbabilities[breakdownProblem];
    } while (randomProbability >= 0);

    if (breakdownProblem != BREAKDOWN_BRAKES_FAILURE)
        return breakdownProblem;

    // Brakes failure can not happen if block brakes are used (so long as there is more than one vehicle)
    // However if this is the case, brake failure should be taken out the equation, otherwise block brake
    // rides have a lower probability to break down due to a random implementation reason.
    if (ride->mode == RIDE_MODE_CONTINUOUS_CIRCUIT_BLOCK_SECTIONED || ride->mode == RIDE_MODE_POWERED_LAUNCH_BLOCK_SECTIONED)
        if (ride->num_vehicles != 1)
            return -1;

    // If brakes failure is disabled, also take it out of the equation (see above comment why)
    if (gCheatsDisableBrakesFailure)
        return -1;

    monthsOld = gDateMonthsElapsed - ride->build_date;
    if (monthsOld < 16 || ride->reliability_percentage > 50)
        return -1;

    return BREAKDOWN_BRAKES_FAILURE;
}

bool Ride::CanBreakDown() const
{
    if (RideAvailableBreakdowns[this->type] == 0)
    {
        return false;
    }

    rct_ride_entry* entry = GetRideEntry();
    return entry != nullptr && !(entry->flags & RIDE_ENTRY_FLAG_CANNOT_BREAK_DOWN);
}

static void choose_random_train_to_breakdown_safe(Ride* ride)
{
    // Prevent integer division by zero in case of hacked ride.
    if (ride->num_vehicles == 0)
        return;

    ride->broken_vehicle = scenario_rand() % ride->num_vehicles;

    // Prevent crash caused by accessing SPRITE_INDEX_NULL on hacked rides.
    // This should probably be cleaned up on import instead.
    while (ride->vehicles[ride->broken_vehicle] == SPRITE_INDEX_NULL && ride->broken_vehicle != 0)
    {
        --ride->broken_vehicle;
    }
}

/**
 *
 *  rct2: 0x006B7348
 */
void ride_prepare_breakdown(Ride* ride, int32_t breakdownReason)
{
    int32_t i;
    uint16_t vehicleSpriteIdx;
    rct_vehicle* vehicle;

    if (ride->lifecycle_flags & (RIDE_LIFECYCLE_BREAKDOWN_PENDING | RIDE_LIFECYCLE_BROKEN_DOWN | RIDE_LIFECYCLE_CRASHED))
        return;

    ride->lifecycle_flags |= RIDE_LIFECYCLE_BREAKDOWN_PENDING;

    ride->breakdown_reason_pending = breakdownReason;
    ride->breakdown_sound_modifier = 0;
    ride->not_fixed_timeout = 0;

    switch (breakdownReason)
    {
        case BREAKDOWN_SAFETY_CUT_OUT:
        case BREAKDOWN_CONTROL_FAILURE:
            // Inspect first station with an exit
            i = ride_get_first_valid_station_exit(ride);
            if (i != -1)
            {
                ride->inspection_station = i;
            }

            break;
        case BREAKDOWN_RESTRAINTS_STUCK_CLOSED:
        case BREAKDOWN_RESTRAINTS_STUCK_OPEN:
        case BREAKDOWN_DOORS_STUCK_CLOSED:
        case BREAKDOWN_DOORS_STUCK_OPEN:
            // Choose a random train and car
            choose_random_train_to_breakdown_safe(ride);
            if (ride->num_cars_per_train != 0)
            {
                ride->broken_car = scenario_rand() % ride->num_cars_per_train;

                // Set flag on broken car
                vehicleSpriteIdx = ride->vehicles[ride->broken_vehicle];
                if (vehicleSpriteIdx != SPRITE_INDEX_NULL)
                {
                    vehicle = GET_VEHICLE(vehicleSpriteIdx);
                    for (i = ride->broken_car; i > 0; i--)
                    {
                        if (vehicle->next_vehicle_on_train == SPRITE_INDEX_NULL)
                        {
                            vehicle = nullptr;
                            break;
                        }
                        else
                        {
                            vehicle = GET_VEHICLE(vehicle->next_vehicle_on_train);
                        }
                    }
                    if (vehicle != nullptr)
                        vehicle->update_flags |= VEHICLE_UPDATE_FLAG_BROKEN_CAR;
                }
            }
            break;
        case BREAKDOWN_VEHICLE_MALFUNCTION:
            // Choose a random train
            choose_random_train_to_breakdown_safe(ride);
            ride->broken_car = 0;

            // Set flag on broken train, first car
            vehicleSpriteIdx = ride->vehicles[ride->broken_vehicle];
            if (vehicleSpriteIdx != SPRITE_INDEX_NULL)
            {
                vehicle = GET_VEHICLE(vehicleSpriteIdx);
                vehicle->update_flags |= VEHICLE_UPDATE_FLAG_BROKEN_TRAIN;
            }
            break;
        case BREAKDOWN_BRAKES_FAILURE:
            // Original code generates a random number but does not use it
            // Unsure if this was supposed to choose a random station (or random station with an exit)
            i = ride_get_first_valid_station_exit(ride);
            if (i != -1)
            {
                ride->inspection_station = i;
            }
            break;
    }
}

/**
 *
 *  rct2: 0x006B74FA
 */
void ride_breakdown_add_news_item(Ride* ride)
{
    ride->FormatNameTo(gCommonFormatArgs);
    if (gConfigNotifications.ride_broken_down)
    {
        news_item_add_to_queue(NEWS_ITEM_RIDE, STR_RIDE_IS_BROKEN_DOWN, ride->id);
    }
}

/**
 *
 *  rct2: 0x006B75C8
 */
static void ride_breakdown_status_update(Ride* ride)
{
    // Warn player if ride hasn't been fixed for ages
    if (ride->lifecycle_flags & RIDE_LIFECYCLE_BROKEN_DOWN)
    {
        ride->not_fixed_timeout++;
        // When there has been a full 255 timeout ticks this
        // will force timeout ticks to keep issuing news every
        // 16 ticks. Note there is no reason to do this.
        if (ride->not_fixed_timeout == 0)
            ride->not_fixed_timeout -= 16;

        if (!(ride->not_fixed_timeout & 15) && ride->mechanic_status != RIDE_MECHANIC_STATUS_FIXING
            && ride->mechanic_status != RIDE_MECHANIC_STATUS_HAS_FIXED_STATION_BRAKES)
        {
            ride->FormatNameTo(gCommonFormatArgs);
            if (gConfigNotifications.ride_warnings)
            {
                news_item_add_to_queue(NEWS_ITEM_RIDE, STR_RIDE_IS_STILL_NOT_FIXED, ride->id);
            }
        }
    }

    ride_mechanic_status_update(ride, ride->mechanic_status);
}

/**
 *
 *  rct2: 0x006B762F
 */
static void ride_mechanic_status_update(Ride* ride, int32_t mechanicStatus)
{
    // Turn a pending breakdown into a breakdown.
    if ((mechanicStatus == RIDE_MECHANIC_STATUS_UNDEFINED || mechanicStatus == RIDE_MECHANIC_STATUS_CALLING
         || mechanicStatus == RIDE_MECHANIC_STATUS_HEADING)
        && (ride->lifecycle_flags & RIDE_LIFECYCLE_BREAKDOWN_PENDING) && !(ride->lifecycle_flags & RIDE_LIFECYCLE_BROKEN_DOWN))
    {
        auto breakdownReason = ride->breakdown_reason_pending;
        if (breakdownReason == BREAKDOWN_SAFETY_CUT_OUT || breakdownReason == BREAKDOWN_BRAKES_FAILURE
            || breakdownReason == BREAKDOWN_CONTROL_FAILURE)
        {
            ride->lifecycle_flags |= RIDE_LIFECYCLE_BROKEN_DOWN;
            ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_MAINTENANCE | RIDE_INVALIDATE_RIDE_LIST
                | RIDE_INVALIDATE_RIDE_MAIN;
            ride->breakdown_reason = breakdownReason;
            ride_breakdown_add_news_item(ride);
        }
    }
    switch (mechanicStatus)
    {
        case RIDE_MECHANIC_STATUS_UNDEFINED:
            if (ride->lifecycle_flags & RIDE_LIFECYCLE_BROKEN_DOWN)
            {
                ride->mechanic_status = RIDE_MECHANIC_STATUS_CALLING;
            }
            break;
        case RIDE_MECHANIC_STATUS_CALLING:
            if (RideAvailableBreakdowns[ride->type] == 0)
            {
                ride->lifecycle_flags &= ~(
                    RIDE_LIFECYCLE_BREAKDOWN_PENDING | RIDE_LIFECYCLE_BROKEN_DOWN | RIDE_LIFECYCLE_DUE_INSPECTION);
                break;
            }

            ride_call_closest_mechanic(ride);
            break;
        case RIDE_MECHANIC_STATUS_HEADING:
        {
            auto mechanic = ride_get_mechanic(ride);
            if (mechanic == nullptr
                || (mechanic->state != PEEP_STATE_HEADING_TO_INSPECTION && mechanic->state != PEEP_STATE_ANSWERING)
                || mechanic->current_ride != ride->id)
            {
                ride->mechanic_status = RIDE_MECHANIC_STATUS_CALLING;
                ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_MAINTENANCE;
                ride_mechanic_status_update(ride, RIDE_MECHANIC_STATUS_CALLING);
            }
            break;
        }
        case RIDE_MECHANIC_STATUS_FIXING:
        {
            auto mechanic = ride_get_mechanic(ride);
            if (mechanic == nullptr
                || (mechanic->state != PEEP_STATE_HEADING_TO_INSPECTION && mechanic->state != PEEP_STATE_FIXING
                    && mechanic->state != PEEP_STATE_INSPECTING && mechanic->state != PEEP_STATE_ANSWERING))
            {
                ride->mechanic_status = RIDE_MECHANIC_STATUS_CALLING;
                ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_MAINTENANCE;
                ride_mechanic_status_update(ride, RIDE_MECHANIC_STATUS_CALLING);
            }
            break;
        }
    }
}

/**
 *
 *  rct2: 0x006B796C
 */
static void ride_call_mechanic(Ride* ride, Peep* mechanic, int32_t forInspection)
{
    mechanic->SetState(forInspection ? PEEP_STATE_HEADING_TO_INSPECTION : PEEP_STATE_ANSWERING);
    mechanic->sub_state = 0;
    ride->mechanic_status = RIDE_MECHANIC_STATUS_HEADING;
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_MAINTENANCE;
    ride->mechanic = mechanic->sprite_index;
    mechanic->current_ride = ride->id;
    mechanic->current_ride_station = ride->inspection_station;
}

/**
 *
 *  rct2: 0x006B76AB
 */
static void ride_call_closest_mechanic(Ride* ride)
{
    auto forInspection = (ride->lifecycle_flags & (RIDE_LIFECYCLE_BREAKDOWN_PENDING | RIDE_LIFECYCLE_BROKEN_DOWN)) == 0;
    auto mechanic = ride_find_closest_mechanic(ride, forInspection);
    if (mechanic != nullptr)
        ride_call_mechanic(ride, mechanic, forInspection);
}

Peep* ride_find_closest_mechanic(Ride* ride, int32_t forInspection)
{
    // Get either exit position or entrance position if there is no exit
    int32_t stationIndex = ride->inspection_station;
    TileCoordsXYZD location = ride_get_exit_location(ride, stationIndex);
    if (location.isNull())
    {
        location = ride_get_entrance_location(ride, stationIndex);
        if (location.isNull())
            return nullptr;
    }

    // Get station start track element and position
    auto mapLocation = location.ToCoordsXYZ();
    TileElement* tileElement = ride_get_station_exit_element(mapLocation);
    if (tileElement == nullptr)
        return nullptr;

    // Set x,y to centre of the station exit for the mechanic search.
    auto centreMapLocation = mapLocation.ToTileCentre();

    return find_closest_mechanic(centreMapLocation.x, centreMapLocation.y, forInspection);
}

/**
 *
 *  rct2: 0x006B774B (forInspection = 0)
 *  rct2: 0x006B78C3 (forInspection = 1)
 */
Peep* find_closest_mechanic(int32_t x, int32_t y, int32_t forInspection)
{
    uint32_t closestDistance, distance;
    uint16_t spriteIndex;
    Peep *peep, *closestMechanic = nullptr;

    closestDistance = UINT_MAX;
    FOR_ALL_STAFF (spriteIndex, peep)
    {
        if (peep->staff_type != STAFF_TYPE_MECHANIC)
            continue;

        if (!forInspection)
        {
            if (peep->state == PEEP_STATE_HEADING_TO_INSPECTION)
            {
                if (peep->sub_state >= 4)
                    continue;
            }
            else if (peep->state != PEEP_STATE_PATROLLING)
                continue;

            if (!(peep->staff_orders & STAFF_ORDERS_FIX_RIDES))
                continue;
        }
        else
        {
            if (peep->state != PEEP_STATE_PATROLLING || !(peep->staff_orders & STAFF_ORDERS_INSPECT_RIDES))
                continue;
        }

        if (map_is_location_in_park({ x, y }))
            if (!staff_is_location_in_patrol(peep, x & 0xFFE0, y & 0xFFE0))
                continue;

        if (peep->x == LOCATION_NULL)
            continue;

        // Manhattan distance
        distance = std::abs(peep->x - x) + std::abs(peep->y - y);
        if (distance < closestDistance)
        {
            closestDistance = distance;
            closestMechanic = peep;
        }
    }

    return closestMechanic;
}

Staff* ride_get_mechanic(Ride* ride)
{
    if (ride->mechanic != SPRITE_INDEX_NULL)
    {
        auto peep = (&(get_sprite(ride->mechanic)->peep))->AsStaff();
        if (peep != nullptr && peep->IsMechanic())
        {
            return peep;
        }
    }
    return nullptr;
}

Staff* ride_get_assigned_mechanic(Ride* ride)
{
    if (ride->lifecycle_flags & RIDE_LIFECYCLE_BROKEN_DOWN)
    {
        if (ride->mechanic_status == RIDE_MECHANIC_STATUS_HEADING || ride->mechanic_status == RIDE_MECHANIC_STATUS_FIXING
            || ride->mechanic_status == RIDE_MECHANIC_STATUS_HAS_FIXED_STATION_BRAKES)
        {
            return ride_get_mechanic(ride);
        }
    }

    return nullptr;
}

#pragma endregion

#pragma region Music functions

/**
 *
 *  rct2: 0x006ABE85
 */
static void ride_music_update(Ride* ride)
{
    if (!(RideData4[ride->type].flags & RIDE_TYPE_FLAG4_MUSIC_ON_DEFAULT)
        && !(RideData4[ride->type].flags & RIDE_TYPE_FLAG4_ALLOW_MUSIC))
    {
        return;
    }

    if (ride->status != RIDE_STATUS_OPEN || !(ride->lifecycle_flags & RIDE_LIFECYCLE_MUSIC))
    {
        ride->music_tune_id = 255;
        return;
    }

    if (ride->type == RIDE_TYPE_CIRCUS)
    {
        uint16_t vehicleSpriteIdx = ride->vehicles[0];
        if (vehicleSpriteIdx != SPRITE_INDEX_NULL)
        {
            rct_vehicle* vehicle = GET_VEHICLE(vehicleSpriteIdx);
            if (vehicle->status != VEHICLE_STATUS_DOING_CIRCUS_SHOW)
            {
                ride->music_tune_id = 255;
                return;
            }
        }
    }

    // Oscillate parameters for a power cut effect when breaking down
    if (ride->lifecycle_flags & (RIDE_LIFECYCLE_BREAKDOWN_PENDING | RIDE_LIFECYCLE_BROKEN_DOWN))
    {
        if (ride->breakdown_reason_pending == BREAKDOWN_CONTROL_FAILURE)
        {
            if (!(gCurrentTicks & 7))
                if (ride->breakdown_sound_modifier != 255)
                    ride->breakdown_sound_modifier++;
        }
        else
        {
            if ((ride->lifecycle_flags & RIDE_LIFECYCLE_BROKEN_DOWN)
                || ride->breakdown_reason_pending == BREAKDOWN_BRAKES_FAILURE
                || ride->breakdown_reason_pending == BREAKDOWN_CONTROL_FAILURE)
            {
                if (ride->breakdown_sound_modifier != 255)
                    ride->breakdown_sound_modifier++;
            }

            if (ride->breakdown_sound_modifier == 255)
            {
                ride->music_tune_id = 255;
                return;
            }
        }
    }

    // Select random tune from available tunes for a music style (of course only merry-go-rounds have more than one tune)
    if (ride->music_tune_id == 255)
    {
        const auto& musicStyleTunes = gRideMusicStyleTuneIds[ride->music];
        auto numTunes = musicStyleTunes.size();
        ride->music_tune_id = musicStyleTunes[util_rand() % numTunes];
        ride->music_position = 0;
        return;
    }

    CoordsXYZ rideCoords = ride->stations[0].GetStart().ToTileCentre();

    int32_t sampleRate = 22050;

    // Alter sample rate for a power cut effect
    if (ride->lifecycle_flags & (RIDE_LIFECYCLE_BREAKDOWN_PENDING | RIDE_LIFECYCLE_BROKEN_DOWN))
    {
        sampleRate = ride->breakdown_sound_modifier * 70;
        if (ride->breakdown_reason_pending != BREAKDOWN_CONTROL_FAILURE)
            sampleRate *= -1;
        sampleRate += 22050;
    }

    ride->music_position = ride_music_params_update(rideCoords, ride, sampleRate, ride->music_position, &ride->music_tune_id);
}

#pragma endregion

#pragma region Measurement functions

/**
 *
 *  rct2: 0x006B64F2
 */
static void ride_measurement_update(Ride& ride, RideMeasurement& measurement)
{
    if (measurement.vehicle_index >= std::size(ride.vehicles))
        return;

    auto spriteIndex = ride.vehicles[measurement.vehicle_index];
    if (spriteIndex == SPRITE_INDEX_NULL)
        return;

    auto vehicle = GET_VEHICLE(spriteIndex);
    if (vehicle == nullptr)
        return;

    if (measurement.flags & RIDE_MEASUREMENT_FLAG_UNLOADING)
    {
        if (vehicle->status != VEHICLE_STATUS_DEPARTING && vehicle->status != VEHICLE_STATUS_TRAVELLING_CABLE_LIFT)
            return;

        measurement.flags &= ~RIDE_MEASUREMENT_FLAG_UNLOADING;
        if (measurement.current_station == vehicle->current_station)
            measurement.current_item = 0;
    }

    if (vehicle->status == VEHICLE_STATUS_UNLOADING_PASSENGERS)
    {
        measurement.flags |= RIDE_MEASUREMENT_FLAG_UNLOADING;
        return;
    }

    uint8_t trackType = (vehicle->track_type >> 2) & 0xFF;
    if (trackType == TRACK_ELEM_BLOCK_BRAKES || trackType == TRACK_ELEM_CABLE_LIFT_HILL
        || trackType == TRACK_ELEM_25_DEG_UP_TO_FLAT || trackType == TRACK_ELEM_60_DEG_UP_TO_FLAT
        || trackType == TRACK_ELEM_DIAG_25_DEG_UP_TO_FLAT || trackType == TRACK_ELEM_DIAG_60_DEG_UP_TO_FLAT)
        if (vehicle->velocity == 0)
            return;

    if (measurement.current_item >= RideMeasurement::MAX_ITEMS)
        return;

    if (measurement.flags & RIDE_MEASUREMENT_FLAG_G_FORCES)
    {
        auto gForces = vehicle_get_g_forces(vehicle);
        gForces.VerticalG = std::clamp(gForces.VerticalG / 8, -127, 127);
        gForces.LateralG = std::clamp(gForces.LateralG / 8, -127, 127);

        if (gScenarioTicks & 1)
        {
            gForces.VerticalG = (gForces.VerticalG + measurement.vertical[measurement.current_item]) / 2;
            gForces.LateralG = (gForces.LateralG + measurement.lateral[measurement.current_item]) / 2;
        }

        measurement.vertical[measurement.current_item] = gForces.VerticalG & 0xFF;
        measurement.lateral[measurement.current_item] = gForces.LateralG & 0xFF;
    }

    auto velocity = std::min(std::abs((vehicle->velocity * 5) >> 16), 255);
    auto altitude = std::min(vehicle->z / 8, 255);

    if (gScenarioTicks & 1)
    {
        velocity = (velocity + measurement.velocity[measurement.current_item]) / 2;
        altitude = (altitude + measurement.altitude[measurement.current_item]) / 2;
    }

    measurement.velocity[measurement.current_item] = velocity & 0xFF;
    measurement.altitude[measurement.current_item] = altitude & 0xFF;

    if (gScenarioTicks & 1)
    {
        measurement.current_item++;
        measurement.num_items = std::max(measurement.num_items, measurement.current_item);
    }
}

/**
 *
 *  rct2: 0x006B6456
 */
void ride_measurements_update()
{
    if (gScreenFlags & SCREEN_FLAGS_SCENARIO_EDITOR)
        return;

    // For each ride measurement
    for (auto& ride : GetRideManager())
    {
        auto measurement = ride.measurement.get();
        if (measurement != nullptr && (ride.lifecycle_flags & RIDE_LIFECYCLE_ON_TRACK) && ride.status != RIDE_STATUS_SIMULATING)
        {
            if (measurement->flags & RIDE_MEASUREMENT_FLAG_RUNNING)
            {
                ride_measurement_update(ride, *measurement);
            }
            else
            {
                // For each vehicle
                for (int32_t j = 0; j < ride.num_vehicles; j++)
                {
                    uint16_t vehicleSpriteIdx = ride.vehicles[j];
                    if (vehicleSpriteIdx != SPRITE_INDEX_NULL)
                    {
                        auto vehicle = GET_VEHICLE(vehicleSpriteIdx);
                        if (vehicle->status == VEHICLE_STATUS_DEPARTING
                            || vehicle->status == VEHICLE_STATUS_TRAVELLING_CABLE_LIFT)
                        {
                            measurement->vehicle_index = j;
                            measurement->current_station = vehicle->current_station;
                            measurement->flags |= RIDE_MEASUREMENT_FLAG_RUNNING;
                            measurement->flags &= ~RIDE_MEASUREMENT_FLAG_UNLOADING;
                            ride_measurement_update(ride, *measurement);
                            break;
                        }
                    }
                }
            }
        }
    }
}

/**
 * If there are more than the threshold of allowed ride measurements, free the non-LRU one.
 */
static void ride_free_old_measurements()
{
    size_t numRideMeasurements;
    do
    {
        Ride* lruRide{};
        numRideMeasurements = 0;
        for (auto& ride : GetRideManager())
        {
            if (ride.measurement != nullptr)
            {
                if (lruRide == nullptr || ride.measurement->last_use_tick > lruRide->measurement->last_use_tick)
                {
                    lruRide = &ride;
                }
                numRideMeasurements++;
            }
        }
        if (numRideMeasurements > MAX_RIDE_MEASUREMENTS && lruRide != nullptr)
        {
            lruRide->measurement = {};
            numRideMeasurements--;
        }
    } while (numRideMeasurements > MAX_RIDE_MEASUREMENTS);
}

std::pair<RideMeasurement*, rct_string_id> ride_get_measurement(Ride* ride)
{
    // Check if ride type supports data logging
    if (!ride_type_has_flag(ride->type, RIDE_TYPE_FLAG_HAS_DATA_LOGGING))
    {
        return { nullptr, STR_DATA_LOGGING_NOT_AVAILABLE_FOR_THIS_TYPE_OF_RIDE };
    }

    // Check if a measurement already exists for this ride
    auto& measurement = ride->measurement;
    if (measurement == nullptr)
    {
        measurement = std::make_unique<RideMeasurement>();
        if (ride_type_has_flag(ride->type, RIDE_TYPE_FLAG_HAS_G_FORCES))
        {
            measurement->flags |= RIDE_MEASUREMENT_FLAG_G_FORCES;
        }
        ride_free_old_measurements();
        assert(ride->measurement != nullptr);
    }

    measurement->last_use_tick = gScenarioTicks;
    if (measurement->flags & 1)
    {
        return { measurement.get(), STR_EMPTY };
    }
    else
    {
        set_format_arg(0, rct_string_id, RideComponentNames[RideNameConvention[ride->type].vehicle].singular);
        set_format_arg(2, rct_string_id, RideComponentNames[RideNameConvention[ride->type].station].singular);
        return { measurement.get(), STR_DATA_LOGGING_WILL_START_WHEN_NEXT_LEAVES };
    }
}

#pragma endregion

#pragma region Colour functions

TrackColour ride_get_track_colour(Ride* ride, int32_t colourScheme)
{
    TrackColour result;
    result.main = ride->track_colour[colourScheme].main;
    result.additional = ride->track_colour[colourScheme].additional;
    result.supports = ride->track_colour[colourScheme].supports;
    return result;
}

vehicle_colour ride_get_vehicle_colour(Ride* ride, int32_t vehicleIndex)
{
    vehicle_colour result;

    if (ride->colour_scheme_type == VEHICLE_COLOUR_SCHEME_PER_VEHICLE)
    {
        // Prevent indexing array out of bounds
        vehicleIndex = std::min(vehicleIndex, MAX_CARS_PER_TRAIN);
    }
    else
    {
        // In this case, only the first car will be set and the rest will be either black or some residual colour.
        vehicleIndex = 0;
    }

    result.main = ride->vehicle_colours[vehicleIndex].Body;
    result.additional_1 = ride->vehicle_colours[vehicleIndex].Trim;
    result.additional_2 = ride->vehicle_colours[vehicleIndex].Ternary;
    return result;
}

static bool ride_does_vehicle_colour_exist(uint8_t ride_sub_type, vehicle_colour* vehicleColour)
{
    for (auto& ride : GetRideManager())
    {
        if (ride.subtype != ride_sub_type)
            continue;
        if (ride.vehicle_colours[0].Body != vehicleColour->main)
            continue;
        return false;
    }
    return true;
}

int32_t ride_get_unused_preset_vehicle_colour(uint8_t ride_sub_type)
{
    if (ride_sub_type >= 128)
    {
        return 0;
    }
    rct_ride_entry* rideEntry = get_ride_entry(ride_sub_type);
    if (rideEntry == nullptr)
    {
        return 0;
    }
    vehicle_colour_preset_list* presetList = rideEntry->vehicle_preset_list;
    if (presetList->count == 0)
        return 0;
    if (presetList->count == 255)
        return 255;

    for (int32_t attempt = 0; attempt < 200; attempt++)
    {
        uint8_t numColourConfigurations = presetList->count;
        int32_t randomConfigIndex = util_rand() % numColourConfigurations;
        vehicle_colour* preset = &presetList->list[randomConfigIndex];

        if (ride_does_vehicle_colour_exist(ride_sub_type, preset))
        {
            return randomConfigIndex;
        }
    }
    return 0;
}

/**
 *
 *  rct2: 0x006DE52C
 */
void ride_set_vehicle_colours_to_random_preset(Ride* ride, uint8_t preset_index)
{
    rct_ride_entry* rideEntry = get_ride_entry(ride->subtype);
    vehicle_colour_preset_list* presetList = rideEntry->vehicle_preset_list;

    if (presetList->count != 0 && presetList->count != 255)
    {
        assert(preset_index < presetList->count);

        ride->colour_scheme_type = RIDE_COLOUR_SCHEME_ALL_SAME;
        vehicle_colour* preset = &presetList->list[preset_index];
        ride->vehicle_colours[0].Body = preset->main;
        ride->vehicle_colours[0].Trim = preset->additional_1;
        ride->vehicle_colours[0].Ternary = preset->additional_2;
    }
    else
    {
        ride->colour_scheme_type = RIDE_COLOUR_SCHEME_DIFFERENT_PER_TRAIN;
        uint32_t count = std::min(presetList->count, (uint8_t)32);
        for (uint32_t i = 0; i < count; i++)
        {
            vehicle_colour* preset = &presetList->list[i];
            ride->vehicle_colours[i].Body = preset->main;
            ride->vehicle_colours[i].Trim = preset->additional_1;
            ride->vehicle_colours[i].Ternary = preset->additional_2;
        }
    }
}

#pragma endregion

#pragma region Reachability

/**
 *
 *  rct2: 0x006B7A5E
 */
void ride_check_all_reachable()
{
    for (auto& ride : GetRideManager())
    {
        if (ride.connected_message_throttle != 0)
            ride.connected_message_throttle--;
        if (ride.status != RIDE_STATUS_OPEN || ride.connected_message_throttle != 0)
            continue;

        if (ride_type_has_flag(ride.type, RIDE_TYPE_FLAG_IS_SHOP))
            ride_shop_connected(&ride);
        else
            ride_entrance_exit_connected(&ride);
    }
}

/**
 *
 *  rct2: 0x006B7C59
 * @return true if the coordinate is reachable or has no entrance, false otherwise
 */
static bool ride_entrance_exit_is_reachable(TileCoordsXYZD coordinates)
{
    if (coordinates.isNull())
        return true;

    TileCoordsXYZ loc{ coordinates.x, coordinates.y, coordinates.z };
    loc -= TileDirectionDelta[coordinates.direction];

    return map_coord_is_connected(loc, coordinates.direction);
}

static void ride_entrance_exit_connected(Ride* ride)
{
    for (int32_t i = 0; i < MAX_STATIONS; ++i)
    {
        auto station_start = ride->stations[i].Start;
        auto entrance = ride_get_entrance_location(ride, i);
        auto exit = ride_get_exit_location(ride, i);

        if (station_start.isNull())
            continue;
        if (!entrance.isNull() && !ride_entrance_exit_is_reachable(entrance))
        {
            // name of ride is parameter of the format string
            ride->FormatNameTo(gCommonFormatArgs);
            if (gConfigNotifications.ride_warnings)
            {
                news_item_add_to_queue(1, STR_ENTRANCE_NOT_CONNECTED, ride->id);
            }
            ride->connected_message_throttle = 3;
        }

        if (!exit.isNull() && !ride_entrance_exit_is_reachable(exit))
        {
            // name of ride is parameter of the format string
            ride->FormatNameTo(gCommonFormatArgs);
            if (gConfigNotifications.ride_warnings)
            {
                news_item_add_to_queue(1, STR_EXIT_NOT_CONNECTED, ride->id);
            }
            ride->connected_message_throttle = 3;
        }
    }
}

static void ride_shop_connected(Ride* ride)
{
    TileCoordsXY shopLoc = ride->stations[0].Start;
    if (shopLoc.isNull())
        return;

    TrackElement* trackElement = nullptr;
    TileElement* tileElement = map_get_first_element_at(shopLoc.ToCoordsXY());
    do
    {
        if (tileElement == nullptr)
            break;
        if (tileElement->GetType() == TILE_ELEMENT_TYPE_TRACK && tileElement->AsTrack()->GetRideIndex() == ride->id)
        {
            trackElement = tileElement->AsTrack();
            break;
        }
    } while (!(tileElement++)->IsLastForTile());

    if (trackElement == nullptr)
        return;

    uint8_t entrance_directions = 0;
    auto track_type = trackElement->GetTrackType();
    ride = get_ride(trackElement->GetRideIndex());
    if (ride == nullptr)
    {
        return;
    }
    if (ride_type_has_flag(ride->type, RIDE_TYPE_FLAG_FLAT_RIDE))
    {
        entrance_directions = FlatRideTrackSequenceProperties[track_type][0] & 0xF;
    }
    else
    {
        entrance_directions = TrackSequenceProperties[track_type][0] & 0xF;
    }

    uint8_t tile_direction = trackElement->GetDirection();
    entrance_directions = rol4(entrance_directions, tile_direction);

    // Now each bit in entrance_directions stands for an entrance direction to check
    if (entrance_directions == 0)
        return;

    for (auto count = 0; entrance_directions != 0; count++)
    {
        if (!(entrance_directions & 1))
        {
            entrance_directions >>= 1;
            continue;
        }
        entrance_directions >>= 1;

        // Flip direction north<->south, east<->west
        uint8_t face_direction = direction_reverse(count);

        int32_t y2 = shopLoc.y - TileDirectionDelta[face_direction].y;
        int32_t x2 = shopLoc.x - TileDirectionDelta[face_direction].x;

        if (map_coord_is_connected({ x2, y2, tileElement->base_height }, face_direction))
            return;
    }

    // Name of ride is parameter of the format string
    ride->FormatNameTo(gCommonFormatArgs);
    if (gConfigNotifications.ride_warnings)
    {
        news_item_add_to_queue(1, STR_ENTRANCE_NOT_CONNECTED, ride->id);
    }

    ride->connected_message_throttle = 3;
}

#pragma endregion

#pragma region Interface

static void ride_track_set_map_tooltip(TileElement* tileElement)
{
    auto rideIndex = tileElement->AsTrack()->GetRideIndex();
    auto ride = get_ride(rideIndex);
    if (ride != nullptr)
    {
        set_map_tooltip_format_arg(0, rct_string_id, STR_RIDE_MAP_TIP);
        auto nameArgLen = ride->FormatNameTo(gMapTooltipFormatArgs + 2);
        ride->FormatStatusTo(gMapTooltipFormatArgs + 2 + nameArgLen);
    }
}

static void ride_queue_banner_set_map_tooltip(TileElement* tileElement)
{
    auto rideIndex = tileElement->AsPath()->GetRideIndex();
    auto ride = get_ride(rideIndex);
    if (ride != nullptr)
    {
        set_map_tooltip_format_arg(0, rct_string_id, STR_RIDE_MAP_TIP);
        auto nameArgLen = ride->FormatNameTo(gMapTooltipFormatArgs + 2);
        ride->FormatStatusTo(gMapTooltipFormatArgs + 2 + nameArgLen);
    }
}

static void ride_station_set_map_tooltip(TileElement* tileElement)
{
    auto rideIndex = tileElement->AsTrack()->GetRideIndex();
    auto ride = get_ride(rideIndex);
    if (ride != nullptr)
    {
        auto stationIndex = tileElement->AsTrack()->GetStationIndex();
        for (int32_t i = stationIndex; i >= 0; i--)
            if (ride->stations[i].Start.isNull())
                stationIndex--;

        size_t argPos = 0;
        set_map_tooltip_format_arg(argPos, rct_string_id, STR_RIDE_MAP_TIP);
        argPos += sizeof(rct_string_id);
        set_map_tooltip_format_arg(argPos, rct_string_id, ride->num_stations <= 1 ? STR_RIDE_STATION : STR_RIDE_STATION_X);
        argPos += sizeof(rct_string_id);
        argPos += ride->FormatNameTo(gMapTooltipFormatArgs + argPos);
        set_map_tooltip_format_arg(
            argPos, rct_string_id, RideComponentNames[RideNameConvention[ride->type].station].capitalised);
        argPos += sizeof(rct_string_id);
        set_map_tooltip_format_arg(argPos, uint16_t, stationIndex + 1);
        argPos += sizeof(uint16_t);
        ride->FormatStatusTo(gMapTooltipFormatArgs + argPos);
    }
}

static void ride_entrance_set_map_tooltip(TileElement* tileElement)
{
    auto rideIndex = tileElement->AsEntrance()->GetRideIndex();
    auto ride = get_ride(rideIndex);
    if (ride != nullptr)
    {
        // Get the station
        auto stationIndex = tileElement->AsEntrance()->GetStationIndex();
        for (int32_t i = stationIndex; i >= 0; i--)
            if (ride->stations[i].Start.isNull())
                stationIndex--;

        if (tileElement->AsEntrance()->GetEntranceType() == ENTRANCE_TYPE_RIDE_ENTRANCE)
        {
            // Get the queue length
            int32_t queueLength = 0;
            if (!ride_get_entrance_location(ride, stationIndex).isNull())
                queueLength = ride->stations[stationIndex].QueueLength;

            size_t argPos = 0;
            set_map_tooltip_format_arg(argPos, rct_string_id, STR_RIDE_MAP_TIP);
            argPos += sizeof(rct_string_id);
            set_map_tooltip_format_arg(
                argPos, rct_string_id, ride->num_stations <= 1 ? STR_RIDE_ENTRANCE : STR_RIDE_STATION_X_ENTRANCE);
            argPos += sizeof(rct_string_id);
            argPos += ride->FormatNameTo(gMapTooltipFormatArgs + argPos);

            // String IDs have an extra pop16 for some reason
            argPos += sizeof(uint16_t);

            set_map_tooltip_format_arg(argPos, uint16_t, stationIndex + 1);
            argPos += sizeof(uint16_t);
            if (queueLength == 0)
            {
                set_map_tooltip_format_arg(argPos, rct_string_id, STR_QUEUE_EMPTY);
            }
            else if (queueLength == 1)
            {
                set_map_tooltip_format_arg(argPos, rct_string_id, STR_QUEUE_ONE_PERSON);
            }
            else
            {
                set_map_tooltip_format_arg(argPos, rct_string_id, STR_QUEUE_PEOPLE);
            }
            argPos += sizeof(rct_string_id);
            set_map_tooltip_format_arg(argPos, uint16_t, queueLength);
        }
        else
        {
            // Get the station
            stationIndex = tileElement->AsEntrance()->GetStationIndex();
            for (int32_t i = stationIndex; i >= 0; i--)
                if (ride->stations[i].Start.isNull())
                    stationIndex--;

            size_t argPos = 0;
            set_map_tooltip_format_arg(
                argPos, rct_string_id, ride->num_stations <= 1 ? STR_RIDE_EXIT : STR_RIDE_STATION_X_EXIT);
            argPos += sizeof(rct_string_id);
            argPos += ride->FormatNameTo(gMapTooltipFormatArgs + 2);

            // String IDs have an extra pop16 for some reason
            argPos += sizeof(uint16_t);

            set_map_tooltip_format_arg(argPos, uint16_t, stationIndex + 1);
        }
    }
}

void ride_set_map_tooltip(TileElement* tileElement)
{
    if (tileElement->GetType() == TILE_ELEMENT_TYPE_ENTRANCE)
    {
        ride_entrance_set_map_tooltip(tileElement);
    }
    else if (tileElement->GetType() == TILE_ELEMENT_TYPE_TRACK)
    {
        if (track_element_is_station(tileElement))
        {
            ride_station_set_map_tooltip(tileElement);
        }
        else
        {
            ride_track_set_map_tooltip(tileElement);
        }
    }
    else if (tileElement->GetType() == TILE_ELEMENT_TYPE_PATH)
    {
        ride_queue_banner_set_map_tooltip(tileElement);
    }
}

static int32_t ride_music_params_update_label_51(
    uint32_t a1, uint8_t* tuneId, Ride* ride, int32_t v32, int32_t pan_x, uint16_t sampleRate)
{
    if (a1 < gRideMusicInfoList[*tuneId].length)
    {
        rct_ride_music_params* ride_music_params = gRideMusicParamsListEnd;
        if (ride_music_params < &gRideMusicParamsList[std::size(gRideMusicParamsList)])
        {
            ride_music_params->ride_id = ride->id;
            ride_music_params->tune_id = *tuneId;
            ride_music_params->offset = a1;
            ride_music_params->volume = v32;
            ride_music_params->pan = pan_x;
            ride_music_params->frequency = sampleRate;
            gRideMusicParamsListEnd++;
        }

        return a1;
    }
    else
    {
        *tuneId = 0xFF;
        return 0;
    }
}

static int32_t ride_music_params_update_label_58(uint32_t position, uint8_t* tuneId)
{
    rct_ride_music_info* ride_music_info = &gRideMusicInfoList[*tuneId];
    position += ride_music_info->offset;
    if (position < ride_music_info->length)
    {
        return position;
    }
    else
    {
        *tuneId = 0xFF;
        return 0;
    }
}

/**
 *
 *  rct2: 0x006BC3AC
 * Update ride music parameters
 * @param x (ax)
 * @param y (cx)
 * @param z (dx)
 * @param sampleRate (di)
 * @param rideIndex (bl)
 * @param position (ebp)
 * @param tuneId (bh)
 * @returns new position (ebp)
 */
int32_t ride_music_params_update(CoordsXYZ rideCoords, Ride* ride, uint16_t sampleRate, uint32_t position, uint8_t* tuneId)
{
    if (!(gScreenFlags & SCREEN_FLAGS_SCENARIO_EDITOR) && !gGameSoundsOff && g_music_tracking_viewport != nullptr)
    {
        const ScreenCoordsXY rotatedCoords = translate_3d_to_2d_with_z(get_current_rotation(), rideCoords);
        rct_viewport* viewport = g_music_tracking_viewport;
        int16_t view_width = viewport->view_width;
        int16_t view_width2 = view_width * 2;
        int16_t view_x = viewport->view_x - view_width2;
        int16_t view_y = viewport->view_y - view_width;
        int16_t view_x2 = view_width2 + view_width2 + viewport->view_width + view_x;
        int16_t view_y2 = view_width + view_width + viewport->view_height + view_y;

        if (view_x >= rotatedCoords.x || view_y >= rotatedCoords.y || view_x2 < rotatedCoords.x || view_y2 < rotatedCoords.y)
        {
            return ride_music_params_update_label_58(position, tuneId);
        }

        int32_t x2 = viewport->x + ((rotatedCoords.x - viewport->view_x) >> viewport->zoom);
        x2 *= 0x10000;
        uint16_t screenwidth = context_get_width();
        if (screenwidth < 64)
        {
            screenwidth = 64;
        }
        int32_t pan_x = ((x2 / screenwidth) - 0x8000) >> 4;

        int32_t y2 = viewport->y + ((rotatedCoords.y - viewport->view_y) >> viewport->zoom);
        y2 *= 0x10000;
        uint16_t screenheight = context_get_height();
        if (screenheight < 64)
        {
            screenheight = 64;
        }
        int32_t pan_y = ((y2 / screenheight) - 0x8000) >> 4;

        uint8_t vol1 = 255;
        uint8_t vol2 = 255;
        int32_t panx2 = pan_x;
        int32_t pany2 = pan_y;
        if (pany2 < 0)
        {
            pany2 = -pany2;
        }
        if (pany2 > 6143)
        {
            pany2 = 6143;
        }
        pany2 -= 2048;
        if (pany2 > 0)
        {
            pany2 = -((pany2 / 4) - 1024) / 4;
            vol1 = (uint8_t)pany2;
            if (pany2 >= 256)
            {
                vol1 = 255;
            }
        }

        if (panx2 < 0)
        {
            panx2 = -panx2;
        }
        if (panx2 > 6143)
        {
            panx2 = 6143;
        }
        panx2 -= 2048;
        if (panx2 > 0)
        {
            panx2 = -((panx2 / 4) - 1024) / 4;
            vol2 = (uint8_t)panx2;
            if (panx2 >= 256)
            {
                vol2 = 255;
            }
        }
        if (vol1 >= vol2)
        {
            vol1 = vol2;
        }
        if (vol1 < gVolumeAdjustZoom * 3)
        {
            vol1 = 0;
        }
        else
        {
            vol1 = vol1 - (gVolumeAdjustZoom * 3);
        }
        int32_t v32 = -(((uint8_t)(-vol1 - 1) * (uint8_t)(-vol1 - 1)) / 16) - 700;
        if (vol1 && v32 >= -4000)
        {
            if (pan_x > 10000)
            {
                pan_x = 10000;
            }
            if (pan_x < -10000)
            {
                pan_x = -10000;
            }
            rct_ride_music* ride_music = &gRideMusicList[0];
            int32_t channel = 0;
            uint32_t a1;
            while (ride_music->ride_id != ride->id || ride_music->tune_id != *tuneId)
            {
                ride_music++;
                channel++;
                if (channel >= AUDIO_MAX_RIDE_MUSIC)
                {
                    rct_ride_music_info* ride_music_info = &gRideMusicInfoList[*tuneId];
                    a1 = position + ride_music_info->offset;

                    return ride_music_params_update_label_51(a1, tuneId, ride, v32, pan_x, sampleRate);
                }
            }
            int32_t playing = Mixer_Channel_IsPlaying(gRideMusicList[channel].sound_channel);
            if (!playing)
            {
                *tuneId = 0xFF;
                return 0;
            }
            a1 = (uint32_t)Mixer_Channel_GetOffset(gRideMusicList[channel].sound_channel);

            return ride_music_params_update_label_51(a1, tuneId, ride, v32, pan_x, sampleRate);
        }
        else
        {
            return ride_music_params_update_label_58(position, tuneId);
        }
    }
    return position;
}

/**
 *  Play/update ride music based on structs updated in 0x006BC3AC
 *  rct2: 0x006BC6D8
 */
void ride_music_update_final()
{
    if ((gScreenFlags & SCREEN_FLAGS_SCENARIO_EDITOR) != 0 || (gScreenFlags & SCREEN_FLAGS_TITLE_DEMO) != 0)
        return;

    // TODO Allow circus music (CSS24) to play if ride music is disabled (that should be sound)
    if (gGameSoundsOff || !gConfigSound.ride_music_enabled)
        return;

    // Stop currently playing music that is not in music params list or not playing?
    for (auto& rideMusic : gRideMusicList)
    {
        if (rideMusic.ride_id != RIDE_ID_NULL)
        {
            rct_ride_music_params* rideMusicParams = &gRideMusicParamsList[0];
            int32_t isPlaying = 0;
            while (rideMusicParams < gRideMusicParamsListEnd && !isPlaying)
            {
                if (rideMusicParams->ride_id == rideMusic.ride_id && rideMusicParams->tune_id == rideMusic.tune_id)
                {
                    isPlaying = Mixer_Channel_IsPlaying(rideMusic.sound_channel);
                    break;
                }
                rideMusicParams++;
            }
            if (!isPlaying)
            {
                Mixer_Stop_Channel(rideMusic.sound_channel);
                rideMusic.ride_id = RIDE_ID_NULL;
            }
        }
    }

    int32_t freeChannelIndex = 0;
    for (rct_ride_music_params* rideMusicParams = &gRideMusicParamsList[0]; rideMusicParams < gRideMusicParamsListEnd;
         rideMusicParams++)
    {
        if (rideMusicParams->ride_id != RIDE_ID_NULL)
        {
            rct_ride_music* rideMusic = &gRideMusicList[0];
            int32_t channelIndex = 0;
            // Look for existing entry, if not found start playing the sound, otherwise update parameters.
            while (rideMusicParams->ride_id != rideMusic->ride_id || rideMusicParams->tune_id != rideMusic->tune_id)
            {
                if (rideMusic->ride_id == RIDE_ID_NULL)
                {
                    freeChannelIndex = channelIndex;
                }
                rideMusic++;
                channelIndex++;
                if (channelIndex >= AUDIO_MAX_RIDE_MUSIC)
                {
                    rct_ride_music_info* ride_music_info = &gRideMusicInfoList[rideMusicParams->tune_id];
                    rct_ride_music* ride_music_3 = &gRideMusicList[freeChannelIndex];
                    ride_music_3->sound_channel = Mixer_Play_Music(ride_music_info->path_id, MIXER_LOOP_NONE, true);
                    if (ride_music_3->sound_channel)
                    {
                        ride_music_3->volume = rideMusicParams->volume;
                        ride_music_3->pan = rideMusicParams->pan;
                        ride_music_3->frequency = rideMusicParams->frequency;
                        ride_music_3->ride_id = rideMusicParams->ride_id;
                        ride_music_3->tune_id = rideMusicParams->tune_id;
                        Mixer_Channel_Volume(ride_music_3->sound_channel, DStoMixerVolume(ride_music_3->volume));
                        Mixer_Channel_Pan(ride_music_3->sound_channel, DStoMixerPan(ride_music_3->pan));
                        Mixer_Channel_Rate(ride_music_3->sound_channel, DStoMixerRate(ride_music_3->frequency));
                        int32_t offset = std::max(0, rideMusicParams->offset - 10000);
                        Mixer_Channel_SetOffset(ride_music_3->sound_channel, offset);

                        // Move circus music to the sound mixer group
                        if (ride_music_info->path_id == PATH_ID_CSS24)
                        {
                            Mixer_Channel_SetGroup(ride_music_3->sound_channel, MIXER_GROUP_SOUND);
                        }
                    }
                    return;
                }
            }

            if (rideMusicParams->volume != rideMusic->volume)
            {
                rideMusic->volume = rideMusicParams->volume;
                Mixer_Channel_Volume(rideMusic->sound_channel, DStoMixerVolume(rideMusic->volume));
            }
            if (rideMusicParams->pan != rideMusic->pan)
            {
                rideMusic->pan = rideMusicParams->pan;
                Mixer_Channel_Pan(rideMusic->sound_channel, DStoMixerPan(rideMusic->pan));
            }
            if (rideMusicParams->frequency != rideMusic->frequency)
            {
                rideMusic->frequency = rideMusicParams->frequency;
                Mixer_Channel_Rate(rideMusic->sound_channel, DStoMixerRate(rideMusic->frequency));
            }
        }
    }
}

#pragma endregion

money32 set_operating_setting(ride_id_t rideId, RideSetSetting setting, uint8_t value)
{
    auto rideSetSetting = RideSetSettingAction(rideId, setting, value);
    auto res = GameActions::Execute(&rideSetSetting);
    return res->Error == GA_ERROR::OK ? 0 : MONEY32_UNDEFINED;
}

money32 set_operating_setting_nested(ride_id_t rideId, RideSetSetting setting, uint8_t value, uint8_t flags)
{
    auto rideSetSetting = RideSetSettingAction(rideId, setting, value);
    rideSetSetting.SetFlags(flags);
    auto res = flags & GAME_COMMAND_FLAG_APPLY ? GameActions::ExecuteNested(&rideSetSetting)
                                               : GameActions::QueryNested(&rideSetSetting);
    return res->Error == GA_ERROR::OK ? 0 : MONEY32_UNDEFINED;
}

/**
 *
 *  rct2: 0x006B4CC1
 */
static int32_t ride_mode_check_valid_station_numbers(Ride* ride)
{
    uint8_t no_stations = 0;
    for (uint8_t station_index = 0; station_index < MAX_STATIONS; ++station_index)
    {
        if (!ride->stations[station_index].Start.isNull())
        {
            no_stations++;
        }
    }

    switch (ride->mode)
    {
        case RIDE_MODE_REVERSE_INCLINE_LAUNCHED_SHUTTLE:
        case RIDE_MODE_POWERED_LAUNCH_PASSTROUGH:
        case RIDE_MODE_POWERED_LAUNCH:
        case RIDE_MODE_LIM_POWERED_LAUNCH:
            if (no_stations <= 1)
                return 1;
            gGameCommandErrorText = STR_UNABLE_TO_OPERATE_WITH_MORE_THAN_ONE_STATION_IN_THIS_MODE;
            return 0;
        case RIDE_MODE_SHUTTLE:
            if (no_stations >= 2)
                return 1;
            gGameCommandErrorText = STR_UNABLE_TO_OPERATE_WITH_LESS_THAN_TWO_STATIONS_IN_THIS_MODE;
            return 0;
    }

    if (ride->type == RIDE_TYPE_GO_KARTS || ride->type == RIDE_TYPE_MINI_GOLF)
    {
        if (no_stations <= 1)
            return 1;
        gGameCommandErrorText = STR_UNABLE_TO_OPERATE_WITH_MORE_THAN_ONE_STATION_IN_THIS_MODE;
        return 0;
    }

    return 1;
}

/**
 * returns stationIndex of first station on success
 * -1 on failure.
 */
static int32_t ride_mode_check_station_present(Ride* ride)
{
    int32_t stationIndex = ride_get_first_valid_station_start(ride);

    if (stationIndex == -1)
    {
        gGameCommandErrorText = STR_NOT_YET_CONSTRUCTED;
        if (ride_type_has_flag(ride->type, RIDE_TYPE_FLAG_HAS_NO_TRACK))
            return -1;

        if (ride->type == RIDE_TYPE_MAZE)
            return -1;

        gGameCommandErrorText = STR_REQUIRES_A_STATION_PLATFORM;
        return -1;
    }

    return stationIndex;
}

/**
 *
 *  rct2: 0x006B5872
 */
static int32_t ride_check_for_entrance_exit(ride_id_t rideIndex)
{
    auto ride = get_ride(rideIndex);
    if (ride == nullptr)
        return 0;

    if (ride_type_has_flag(ride->type, RIDE_TYPE_FLAG_IS_SHOP))
        return 1;

    int32_t i;
    uint8_t entrance = 0;
    uint8_t exit = 0;
    for (i = 0; i < MAX_STATIONS; i++)
    {
        if (ride->stations[i].Start.isNull())
            continue;

        if (!ride_get_entrance_location(ride, i).isNull())
        {
            entrance = 1;
        }

        if (!ride_get_exit_location(ride, i).isNull())
        {
            exit = 1;
        }

        // If station start and no entrance/exit
        // Sets same error message as no entrance
        if (ride_get_exit_location(ride, i).isNull() && ride_get_entrance_location(ride, i).isNull())
        {
            entrance = 0;
            break;
        }
    }

    if (entrance == 0)
    {
        gGameCommandErrorText = STR_ENTRANCE_NOT_YET_BUILT;
        return 0;
    }

    if (exit == 0)
    {
        gGameCommandErrorText = STR_EXIT_NOT_YET_BUILT;
        return 0;
    }

    return 1;
}

/**
 *
 *  rct2: 0x006B5952
 */
static void sub_6B5952(Ride* ride)
{
    for (int32_t i = 0; i < MAX_STATIONS; i++)
    {
        auto location = ride_get_entrance_location(ride, i);
        if (location.isNull())
            continue;

        auto mapLocation = location.ToCoordsXYZ();

        // This will fire for every entrance on this x, y and z, regardless whether that actually belongs to
        // the ride or not.
        TileElement* tileElement = map_get_first_element_at(location.ToCoordsXY());
        if (tileElement != nullptr)
        {
            do
            {
                if (tileElement->GetType() != TILE_ELEMENT_TYPE_ENTRANCE)
                    continue;
                if (tileElement->GetBaseZ() != mapLocation.z)
                    continue;

                int32_t direction = tileElement->GetDirection();
                footpath_chain_ride_queue(ride->id, i, mapLocation, tileElement, direction_reverse(direction));
            } while (!(tileElement++)->IsLastForTile());
        }
    }
}

/**
 *
 *  rct2: 0x006D3319
 */
static int32_t ride_check_block_brakes(CoordsXYE* input, CoordsXYE* output)
{
    rct_window* w;
    track_circuit_iterator it;
    int32_t type;

    ride_id_t rideIndex = input->element->AsTrack()->GetRideIndex();
    w = window_find_by_class(WC_RIDE_CONSTRUCTION);
    if (w != nullptr && _rideConstructionState != RIDE_CONSTRUCTION_STATE_0 && _currentRideIndex == rideIndex)
        ride_construction_invalidate_current_track();

    track_circuit_iterator_begin(&it, *input);
    while (track_circuit_iterator_next(&it))
    {
        if (it.current.element->AsTrack()->GetTrackType() == TRACK_ELEM_BLOCK_BRAKES)
        {
            type = it.last.element->AsTrack()->GetTrackType();
            if (type == TRACK_ELEM_END_STATION)
            {
                gGameCommandErrorText = STR_BLOCK_BRAKES_CANNOT_BE_USED_DIRECTLY_AFTER_STATION;
                *output = it.current;
                return 0;
            }
            if (type == TRACK_ELEM_BLOCK_BRAKES)
            {
                gGameCommandErrorText = STR_BLOCK_BRAKES_CANNOT_BE_USED_DIRECTLY_AFTER_EACH_OTHER;
                *output = it.current;
                return 0;
            }
            if (it.last.element->AsTrack()->HasChain() && type != TRACK_ELEM_LEFT_CURVED_LIFT_HILL
                && type != TRACK_ELEM_RIGHT_CURVED_LIFT_HILL)
            {
                gGameCommandErrorText = STR_BLOCK_BRAKES_CANNOT_BE_USED_DIRECTLY_AFTER_THE_TOP_OF_THIS_LIFT_HILL;
                *output = it.current;
                return 0;
            }
        }
    }
    if (!it.looped)
    {
        // Not sure why this is the case...
        gGameCommandErrorText = STR_BLOCK_BRAKES_CANNOT_BE_USED_DIRECTLY_AFTER_STATION;
        *output = it.last;
        return 0;
    }

    return 1;
}

/**
 * Iterates along the track until an inversion (loop, corkscrew, barrel roll etc.) track piece is reached.
 * @param input The start track element and position.
 * @param output The first track element and position which is classified as an inversion.
 * @returns true if an inversion track piece is found, otherwise false.
 *  rct2: 0x006CB149
 */
static bool ride_check_track_contains_inversions(CoordsXYE* input, CoordsXYE* output)
{
    ride_id_t rideIndex = input->element->AsTrack()->GetRideIndex();
    auto ride = get_ride(rideIndex);
    if (ride != nullptr && ride->type == RIDE_TYPE_MAZE)
        return true;

    rct_window* w = window_find_by_class(WC_RIDE_CONSTRUCTION);
    if (w != nullptr && _rideConstructionState != RIDE_CONSTRUCTION_STATE_0 && rideIndex == _currentRideIndex)
    {
        ride_construction_invalidate_current_track();
    }

    bool moveSlowIt = true;
    track_circuit_iterator it, slowIt;
    track_circuit_iterator_begin(&it, *input);
    slowIt = it;

    while (track_circuit_iterator_next(&it))
    {
        int32_t trackType = it.current.element->AsTrack()->GetTrackType();
        if (TrackFlags[trackType] & TRACK_ELEM_FLAG_INVERSION_TO_NORMAL)
        {
            *output = it.current;
            return true;
        }

        // Prevents infinite loops
        moveSlowIt = !moveSlowIt;
        if (moveSlowIt)
        {
            track_circuit_iterator_next(&slowIt);
            if (track_circuit_iterators_match(&it, &slowIt))
            {
                return false;
            }
        }
    }
    return false;
}

/**
 * Iterates along the track until a banked track piece is reached.
 * @param input The start track element and position.
 * @param output The first track element and position which is banked.
 * @returns true if a banked track piece is found, otherwise false.
 *  rct2: 0x006CB1D3
 */
static bool ride_check_track_contains_banked(CoordsXYE* input, CoordsXYE* output)
{
    auto rideIndex = input->element->AsTrack()->GetRideIndex();
    auto ride = get_ride(rideIndex);
    if (ride == nullptr)
        return false;

    if (ride->type == RIDE_TYPE_MAZE)
        return true;

    rct_window* w = window_find_by_class(WC_RIDE_CONSTRUCTION);
    if (w != nullptr && _rideConstructionState != RIDE_CONSTRUCTION_STATE_0 && rideIndex == _currentRideIndex)
    {
        ride_construction_invalidate_current_track();
    }

    bool moveSlowIt = true;
    track_circuit_iterator it, slowIt;
    track_circuit_iterator_begin(&it, *input);
    slowIt = it;

    while (track_circuit_iterator_next(&it))
    {
        int32_t trackType = output->element->AsTrack()->GetTrackType();
        if (TrackFlags[trackType] & TRACK_ELEM_FLAG_BANKED)
        {
            *output = it.current;
            return true;
        }

        // Prevents infinite loops
        moveSlowIt = !moveSlowIt;
        if (moveSlowIt)
        {
            track_circuit_iterator_next(&slowIt);
            if (track_circuit_iterators_match(&it, &slowIt))
            {
                return false;
            }
        }
    }
    return false;
}

/**
 *
 *  rct2: 0x006CB25D
 */
static int32_t ride_check_station_length(CoordsXYE* input, CoordsXYE* output)
{
    rct_window* w = window_find_by_class(WC_RIDE_CONSTRUCTION);
    if (w != nullptr && _rideConstructionState != RIDE_CONSTRUCTION_STATE_0
        && _currentRideIndex == input->element->AsTrack()->GetRideIndex())
    {
        ride_construction_invalidate_current_track();
    }

    output->x = input->x;
    output->y = input->y;
    output->element = input->element;
    track_begin_end trackBeginEnd;
    while (track_block_get_previous(output->x, output->y, output->element, &trackBeginEnd))
    {
        output->x = trackBeginEnd.begin_x;
        output->y = trackBeginEnd.begin_y;
        output->element = trackBeginEnd.begin_element;
    }

    int32_t num_station_elements = 0;
    CoordsXYE last_good_station = *output;

    do
    {
        if (TrackSequenceProperties[output->element->AsTrack()->GetTrackType()][0] & TRACK_SEQUENCE_FLAG_ORIGIN)
        {
            num_station_elements++;
            last_good_station = *output;
        }
        else
        {
            if (num_station_elements == 0)
                continue;
            if (num_station_elements == 1)
            {
                return 0;
            }
            num_station_elements = 0;
        }
    } while (track_block_get_next(output, output, nullptr, nullptr));

    // Prevent returning a pointer to a map element with no track.
    *output = last_good_station;
    if (num_station_elements == 1)
        return 0;

    return 1;
}

/**
 *
 *  rct2: 0x006CB2DA
 */
static bool ride_check_start_and_end_is_station(CoordsXYE* input)
{
    int32_t trackType;
    CoordsXYE trackBack, trackFront;

    ride_id_t rideIndex = input->element->AsTrack()->GetRideIndex();
    auto ride = get_ride(rideIndex);
    if (ride == nullptr)
        return false;

    auto w = window_find_by_class(WC_RIDE_CONSTRUCTION);
    if (w != nullptr && _rideConstructionState != RIDE_CONSTRUCTION_STATE_0 && rideIndex == _currentRideIndex)
    {
        ride_construction_invalidate_current_track();
    }

    // Check back of the track
    track_get_back(input, &trackBack);
    trackType = trackBack.element->AsTrack()->GetTrackType();
    if (!(TrackSequenceProperties[trackType][0] & TRACK_SEQUENCE_FLAG_ORIGIN))
    {
        return false;
    }
    ride->ChairliftBullwheelLocation[0] = TileCoordsXYZ{ CoordsXYZ{ trackBack.x, trackBack.y, trackBack.element->GetBaseZ() } };

    // Check front of the track
    track_get_front(input, &trackFront);
    trackType = trackFront.element->AsTrack()->GetTrackType();
    if (!(TrackSequenceProperties[trackType][0] & TRACK_SEQUENCE_FLAG_ORIGIN))
    {
        return false;
    }
    ride->ChairliftBullwheelLocation[1] = TileCoordsXYZ{ CoordsXYZ{ trackFront.x, trackFront.y,
                                                                    trackFront.element->GetBaseZ() } };
    return true;
}

/**
 * Sets the position and direction of the returning point on the track of a boat hire ride. This will either be the end of the
 * station or the last track piece from the end of the direction.
 *  rct2: 0x006B4D39
 */
static void ride_set_boat_hire_return_point(Ride* ride, CoordsXYE* startElement)
{
    int32_t trackType = -1;
    int32_t returnX = startElement->x;
    int32_t returnY = startElement->y;
    int32_t startX = returnX;
    int32_t startY = returnY;
    TileElement* returnTrackElement = startElement->element;
    track_begin_end trackBeginEnd;
    while (track_block_get_previous(returnX, returnY, returnTrackElement, &trackBeginEnd))
    {
        // If previous track is back to the starting x, y, then break loop (otherwise possible infinite loop)
        if (trackType != -1 && startX == trackBeginEnd.begin_x && startY == trackBeginEnd.begin_y)
            break;

        int32_t x = trackBeginEnd.begin_x;
        int32_t y = trackBeginEnd.begin_y;
        int32_t z = trackBeginEnd.begin_z;
        int32_t direction = trackBeginEnd.begin_direction;
        trackType = trackBeginEnd.begin_element->AsTrack()->GetTrackType();
        sub_6C683D(&x, &y, &z, direction, trackType, 0, &returnTrackElement, 0);
        returnX = x;
        returnY = y;
    };

    trackType = returnTrackElement->AsTrack()->GetTrackType();
    int32_t elementReturnDirection = TrackCoordinates[trackType].rotation_begin;
    ride->boat_hire_return_direction = returnTrackElement->GetDirectionWithOffset(elementReturnDirection);
    ride->boat_hire_return_position.x = returnX >> 5;
    ride->boat_hire_return_position.y = returnY >> 5;
}

/**
 *
 *  rct2: 0x006B4D39
 */
static void ride_set_maze_entrance_exit_points(Ride* ride)
{
    // Needs room for an entrance and an exit per station, plus one position for the list terminator.
    TileCoordsXYZD positions[(MAX_STATIONS * 2) + 1];

    // Create a list of all the entrance and exit positions
    TileCoordsXYZD* position = positions;
    for (int32_t i = 0; i < MAX_STATIONS; i++)
    {
        const auto entrance = ride_get_entrance_location(ride, i);
        const auto exit = ride_get_exit_location(ride, i);

        if (!entrance.isNull())
        {
            *position++ = entrance;
        }
        if (!exit.isNull())
        {
            *position++ = exit;
        }
    }
    (*position++).setNull();

    // Enumerate entrance and exit positions
    for (position = positions; !(*position).isNull(); position++)
    {
        int32_t x = (*position).x << 5;
        int32_t y = (*position).y << 5;
        int32_t z = (*position).z;

        TileElement* tileElement = map_get_first_element_at(position->ToCoordsXY());
        do
        {
            if (tileElement == nullptr)
                break;
            if (tileElement->GetType() != TILE_ELEMENT_TYPE_ENTRANCE)
                continue;
            if (tileElement->AsEntrance()->GetEntranceType() != ENTRANCE_TYPE_RIDE_ENTRANCE
                && tileElement->AsEntrance()->GetEntranceType() != ENTRANCE_TYPE_RIDE_EXIT)
            {
                continue;
            }
            if (tileElement->base_height != z)
                continue;

            maze_entrance_hedge_removal(x, y, tileElement);
        } while (!(tileElement++)->IsLastForTile());
    }
}

/**
 * Sets a flag on all the track elements that can be the start of a circuit block. i.e. where a train can start.
 *  rct2: 0x006B4E6B
 */
static void ride_set_block_points(CoordsXYE* startElement)
{
    CoordsXYE currentElement = *startElement;
    do
    {
        int32_t trackType = currentElement.element->AsTrack()->GetTrackType();
        switch (trackType)
        {
            case TRACK_ELEM_END_STATION:
            case TRACK_ELEM_CABLE_LIFT_HILL:
            case TRACK_ELEM_25_DEG_UP_TO_FLAT:
            case TRACK_ELEM_60_DEG_UP_TO_FLAT:
            case TRACK_ELEM_DIAG_25_DEG_UP_TO_FLAT:
            case TRACK_ELEM_DIAG_60_DEG_UP_TO_FLAT:
            case TRACK_ELEM_BLOCK_BRAKES:
                currentElement.element->AsTrack()->SetBlockBrakeClosed(false);
                break;
        }
    } while (track_block_get_next(&currentElement, &currentElement, nullptr, nullptr)
             && currentElement.element != startElement->element);
}

/**
 *
 *  rct2: 0x006B4D26
 */
static void ride_set_start_finish_points(ride_id_t rideIndex, CoordsXYE* startElement)
{
    auto ride = get_ride(rideIndex);
    if (ride == nullptr)
        return;

    switch (ride->type)
    {
        case RIDE_TYPE_BOAT_HIRE:
            ride_set_boat_hire_return_point(ride, startElement);
            break;
        case RIDE_TYPE_MAZE:
            ride_set_maze_entrance_exit_points(ride);
            break;
    }

    if (ride->IsBlockSectioned() && !(ride->lifecycle_flags & RIDE_LIFECYCLE_ON_TRACK))
    {
        ride_set_block_points(startElement);
    }
}

/**
 *
 *  rct2: 0x0069ED9E
 */
static int32_t count_free_misc_sprite_slots()
{
    int32_t miscSpriteCount = gSpriteListCount[SPRITE_LIST_MISC];
    int32_t remainingSpriteCount = gSpriteListCount[SPRITE_LIST_FREE];
    return std::max(0, miscSpriteCount + remainingSpriteCount - 300);
}

static constexpr const CoordsXY word_9A3AB4[4] = {
    { 0, 0 },
    { 0, -96 },
    { -96, -96 },
    { -96, 0 },
};

// clang-format off
static constexpr const CoordsXY word_9A2A60[] = {
    { 0, 16 },
    { 16, 31 },
    { 31, 16 },
    { 16, 0 },
    { 16, 16 },
    { 64, 64 },
    { 64, -32 },
    { -32, -32 },
    { -32, 64 },
};
// clang-format on

/**
 *
 *  rct2: 0x006DD90D
 */
static rct_vehicle* vehicle_create_car(
    ride_id_t rideIndex, int32_t vehicleEntryIndex, int32_t carIndex, int32_t vehicleIndex, int32_t x, int32_t y, int32_t z,
    int32_t* remainingDistance, TileElement* tileElement)
{
    auto ride = get_ride(rideIndex);
    if (ride == nullptr)
        return nullptr;

    auto rideEntry = ride->GetRideEntry();
    if (rideEntry == nullptr)
        return nullptr;

    auto vehicleEntry = &rideEntry->vehicles[vehicleEntryIndex];
    auto vehicle = &create_sprite(SPRITE_IDENTIFIER_VEHICLE)->vehicle;
    if (vehicle == nullptr)
        return nullptr;

    vehicle->sprite_identifier = SPRITE_IDENTIFIER_VEHICLE;
    vehicle->ride = rideIndex;
    vehicle->ride_subtype = ride->subtype;

    vehicle->vehicle_type = vehicleEntryIndex;
    vehicle->type = carIndex == 0 ? VEHICLE_TYPE_HEAD : VEHICLE_TYPE_TAIL;
    vehicle->var_44 = ror32(vehicleEntry->spacing, 10) & 0xFFFF;
    auto edx = vehicleEntry->spacing >> 1;
    *remainingDistance -= edx;
    vehicle->remaining_distance = *remainingDistance;
    if (!(vehicleEntry->flags & VEHICLE_ENTRY_FLAG_GO_KART))
    {
        *remainingDistance -= edx;
    }

    // loc_6DD9A5:
    vehicle->sprite_width = vehicleEntry->sprite_width;
    vehicle->sprite_height_negative = vehicleEntry->sprite_height_negative;
    vehicle->sprite_height_positive = vehicleEntry->sprite_height_positive;
    vehicle->mass = vehicleEntry->car_mass;
    vehicle->num_seats = vehicleEntry->num_seats;
    vehicle->speed = vehicleEntry->powered_max_speed;
    vehicle->powered_acceleration = vehicleEntry->powered_acceleration;
    vehicle->velocity = 0;
    vehicle->acceleration = 0;
    vehicle->swing_sprite = 0;
    vehicle->swinging_car_var_0 = 0;
    vehicle->var_4E = 0;
    vehicle->restraints_position = 0;
    vehicle->spin_sprite = 0;
    vehicle->spin_speed = 0;
    vehicle->sound2_flags = 0;
    vehicle->sound1_id = SoundId::Null;
    vehicle->sound2_id = SoundId::Null;
    vehicle->next_vehicle_on_train = SPRITE_INDEX_NULL;
    vehicle->var_C4 = 0;
    vehicle->animation_frame = 0;
    vehicle->var_C8 = 0;
    vehicle->scream_sound_id = SoundId::Null;
    vehicle->vehicle_sprite_type = 0;
    vehicle->bank_rotation = 0;
    vehicle->target_seat_rotation = 4;
    vehicle->seat_rotation = 4;
    for (int32_t i = 0; i < 32; i++)
    {
        vehicle->peep[i] = SPRITE_INDEX_NULL;
    }

    if (vehicleEntry->flags & VEHICLE_ENTRY_FLAG_DODGEM_CAR_PLACEMENT)
    {
        // loc_6DDCA4:
        vehicle->var_CD = 0;
        int32_t direction = tileElement->GetDirection();
        x += word_9A3AB4[direction].x;
        y += word_9A3AB4[direction].y;
        z = tileElement->GetBaseZ();
        vehicle->track_x = x;
        vehicle->track_y = y;
        vehicle->track_z = z;
        vehicle->current_station = tileElement->AsTrack()->GetStationIndex();

        z += RideData5[ride->type].z_offset;

        vehicle->track_type = tileElement->AsTrack()->GetTrackType() << 2;
        vehicle->track_progress = 0;
        vehicle->SetState(VEHICLE_STATUS_MOVING_TO_END_OF_STATION);
        vehicle->update_flags = 0;

        CoordsXY chosenLoc;
        // loc_6DDD26:
        do
        {
            vehicle->sprite_direction = scenario_rand() & 0x1E;
            chosenLoc.y = y + (scenario_rand() & 0xFF);
            chosenLoc.x = x + (scenario_rand() & 0xFF);
        } while (vehicle_update_dodgems_collision(vehicle, chosenLoc.x, chosenLoc.y, nullptr));

        sprite_move(chosenLoc.x, chosenLoc.y, z, (rct_sprite*)vehicle);
    }
    else
    {
        int16_t dl = 0;
        if (vehicleEntry->flags & VEHICLE_ENTRY_FLAG_CHAIRLIFT)
        {
            dl = 1;
        }

        if (vehicleEntry->flags & VEHICLE_ENTRY_FLAG_GO_KART)
        {
            // Choose which lane Go Kart should start in
            dl = 5;
            if (vehicleIndex & 1)
            {
                dl = 6;
            }
        }
        if (vehicleEntry->flags & VEHICLE_ENTRY_FLAG_MINI_GOLF)
        {
            dl = 9;
            vehicle->var_D3 = 0;
            vehicle->mini_golf_current_animation = 0;
            vehicle->mini_golf_flags = 0;
        }
        if (vehicleEntry->flags & VEHICLE_ENTRY_FLAG_4)
        {
            if (vehicle->IsHead())
            {
                dl = 15;
            }
        }
        if (vehicleEntry->flags & VEHICLE_ENTRY_FLAG_5)
        {
            dl = 16;
        }
        vehicle->var_CD = dl;

        vehicle->track_x = x;
        vehicle->track_y = y;

        int32_t direction = tileElement->GetDirection();
        vehicle->sprite_direction = direction << 3;

        if (ride->type == RIDE_TYPE_SPACE_RINGS)
        {
            direction = 4;
        }
        else
        {
            if (ride_type_has_flag(ride->type, RIDE_TYPE_FLAG_16))
            {
                if (RideConstructionDefaultTrackType[ride->type] != FLAT_TRACK_ELEM_1_X_4_B)
                {
                    if (RideConstructionDefaultTrackType[ride->type] != FLAT_TRACK_ELEM_1_X_4_A)
                    {
                        if (ride->type == RIDE_TYPE_ENTERPRISE)
                        {
                            direction += 5;
                        }
                        else
                        {
                            direction = 4;
                        }
                    }
                }
            }
        }

        x += word_9A2A60[direction].x;
        y += word_9A2A60[direction].y;
        vehicle->track_z = tileElement->GetBaseZ();

        vehicle->current_station = tileElement->AsTrack()->GetStationIndex();
        z = tileElement->GetBaseZ();
        z += RideData5[ride->type].z_offset;

        sprite_move(x, y, z, (rct_sprite*)vehicle);
        vehicle->track_type = (tileElement->AsTrack()->GetTrackType() << 2) | (vehicle->sprite_direction >> 3);
        vehicle->track_progress = 31;
        if (vehicleEntry->flags & VEHICLE_ENTRY_FLAG_MINI_GOLF)
        {
            vehicle->track_progress = 15;
        }
        vehicle->update_flags = VEHICLE_UPDATE_FLAG_1;
        if (vehicleEntry->flags & VEHICLE_ENTRY_FLAG_HAS_INVERTED_SPRITE_SET)
        {
            if (tileElement->AsTrack()->IsInverted())
            {
                vehicle->update_flags |= VEHICLE_UPDATE_FLAG_USE_INVERTED_SPRITES;
            }
        }
        vehicle->SetState(VEHICLE_STATUS_MOVING_TO_END_OF_STATION);
    }

    // loc_6DDD5E:
    vehicle->num_peeps = 0;
    vehicle->next_free_seat = 0;
    return vehicle;
}

/**
 *
 *  rct2: 0x006DD84C
 */
static train_ref vehicle_create_train(
    ride_id_t rideIndex, int32_t x, int32_t y, int32_t z, int32_t vehicleIndex, int32_t* remainingDistance,
    TileElement* tileElement)
{
    train_ref train = { nullptr, nullptr };
    auto ride = get_ride(rideIndex);
    if (ride != nullptr)
    {
        for (int32_t carIndex = 0; carIndex < ride->num_cars_per_train; carIndex++)
        {
            auto vehicle = ride_entry_get_vehicle_at_position(ride->subtype, ride->num_cars_per_train, carIndex);
            auto car = vehicle_create_car(rideIndex, vehicle, carIndex, vehicleIndex, x, y, z, remainingDistance, tileElement);
            if (car == nullptr)
                break;

            if (carIndex == 0)
            {
                train.head = car;
            }
            else
            {
                // Link the previous car with this car
                train.tail->next_vehicle_on_train = car->sprite_index;
                train.tail->next_vehicle_on_ride = car->sprite_index;
                car->prev_vehicle_on_ride = train.tail->sprite_index;
            }
            train.tail = car;
        }
    }
    return train;
}

static void vehicle_create_trains(ride_id_t rideIndex, int32_t x, int32_t y, int32_t z, TileElement* tileElement)
{
    auto ride = get_ride(rideIndex);
    if (ride == nullptr)
        return;

    train_ref firstTrain = {};
    train_ref lastTrain = {};
    int32_t remainingDistance = 0;

    for (int32_t vehicleIndex = 0; vehicleIndex < ride->num_vehicles; vehicleIndex++)
    {
        if (ride->IsBlockSectioned())
        {
            remainingDistance = 0;
        }
        train_ref train = vehicle_create_train(rideIndex, x, y, z, vehicleIndex, &remainingDistance, tileElement);
        if (vehicleIndex == 0)
        {
            firstTrain = train;
        }
        else
        {
            // Link the end of the previous train with the front of this train
            lastTrain.tail->next_vehicle_on_ride = train.head->sprite_index;
            train.head->prev_vehicle_on_ride = lastTrain.tail->sprite_index;
        }
        lastTrain = train;

        // Add train to ride vehicle list
        move_sprite_to_list((rct_sprite*)train.head, SPRITE_LIST_VEHICLE_HEAD);
        for (int32_t i = 0; i <= MAX_VEHICLES_PER_RIDE; i++)
        {
            if (ride->vehicles[i] == SPRITE_INDEX_NULL)
            {
                ride->vehicles[i] = train.head->sprite_index;
                break;
            }
        }
    }

    // Link the first train and last train together. Nullptr checks are there to keep Clang happy.
    if (lastTrain.tail != nullptr)
        firstTrain.head->prev_vehicle_on_ride = lastTrain.tail->sprite_index;
    if (firstTrain.head != nullptr)
        lastTrain.tail->next_vehicle_on_ride = firstTrain.head->sprite_index;
}

static void vehicle_unset_update_flag_b1(rct_vehicle* head)
{
    rct_vehicle* vehicle = head;
    while (true)
    {
        vehicle->update_flags &= ~VEHICLE_UPDATE_FLAG_1;
        uint16_t spriteIndex = vehicle->next_vehicle_on_train;
        if (spriteIndex == SPRITE_INDEX_NULL)
        {
            break;
        }
        vehicle = GET_VEHICLE(spriteIndex);
    }
}

/**
 *
 *  rct2: 0x006DDE9E
 */
static void ride_create_vehicles_find_first_block(Ride* ride, CoordsXYE* outXYElement)
{
    rct_vehicle* vehicle = GET_VEHICLE(ride->vehicles[0]);
    auto curTrackPos = CoordsXYZ{ vehicle->track_x, vehicle->track_y, vehicle->track_z };
    auto curTrackElement = map_get_track_element_at(curTrackPos);

    assert(curTrackElement != nullptr);

    int32_t x = curTrackPos.x;
    int32_t y = curTrackPos.y;
    auto trackElement = curTrackElement;
    track_begin_end trackBeginEnd;
    while (track_block_get_previous(x, y, reinterpret_cast<TileElement*>(trackElement), &trackBeginEnd))
    {
        x = trackBeginEnd.end_x;
        y = trackBeginEnd.end_y;
        trackElement = trackBeginEnd.begin_element->AsTrack();
        if (x == curTrackPos.x && y == curTrackPos.y && trackElement == curTrackElement)
        {
            break;
        }

        int32_t trackType = trackElement->GetTrackType();
        switch (trackType)
        {
            case TRACK_ELEM_25_DEG_UP_TO_FLAT:
            case TRACK_ELEM_60_DEG_UP_TO_FLAT:
                if (trackElement->HasChain())
                {
                    outXYElement->x = x;
                    outXYElement->y = y;
                    outXYElement->element = reinterpret_cast<TileElement*>(trackElement);
                    return;
                }
                break;
            case TRACK_ELEM_DIAG_25_DEG_UP_TO_FLAT:
            case TRACK_ELEM_DIAG_60_DEG_UP_TO_FLAT:
                if (trackElement->HasChain())
                {
                    TileElement* tileElement = map_get_track_element_at_of_type_seq(
                        { trackBeginEnd.begin_x, trackBeginEnd.begin_y, trackBeginEnd.begin_z }, trackType, 0);

                    if (tileElement != nullptr)
                    {
                        outXYElement->x = trackBeginEnd.begin_x;
                        outXYElement->y = trackBeginEnd.begin_y;
                        outXYElement->element = tileElement;
                        return;
                    }
                }
                break;
            case TRACK_ELEM_END_STATION:
            case TRACK_ELEM_CABLE_LIFT_HILL:
            case TRACK_ELEM_BLOCK_BRAKES:
                outXYElement->x = x;
                outXYElement->y = y;
                outXYElement->element = reinterpret_cast<TileElement*>(trackElement);
                return;
        }
    }

    outXYElement->x = curTrackPos.x;
    outXYElement->y = curTrackPos.y;
    outXYElement->element = reinterpret_cast<TileElement*>(curTrackElement);
}

/**
 *
 *  rct2: 0x006DD84C
 */
static bool ride_create_vehicles(Ride* ride, CoordsXYE* element, int32_t isApplying)
{
    ride->UpdateMaxVehicles();
    if (ride->subtype == RIDE_ENTRY_INDEX_NULL)
    {
        return true;
    }

    // Check if there are enough free sprite slots for all the vehicles
    int32_t totalCars = ride->num_vehicles * ride->num_cars_per_train;
    if (totalCars > count_free_misc_sprite_slots())
    {
        gGameCommandErrorText = STR_UNABLE_TO_CREATE_ENOUGH_VEHICLES;
        return false;
    }

    if (!isApplying)
    {
        return true;
    }

    TileElement* tileElement = element->element;
    int32_t x = element->x;
    int32_t y = element->y;
    int32_t z = element->element->base_height;
    int32_t direction = tileElement->GetDirection();

    //
    if (ride->mode == RIDE_MODE_STATION_TO_STATION)
    {
        x = element->x - CoordsDirectionDelta[direction].x;
        y = element->y - CoordsDirectionDelta[direction].y;

        tileElement = reinterpret_cast<TileElement*>(map_get_track_element_at({ x, y, z << 3 }));

        z = tileElement->base_height;
        direction = tileElement->GetDirection();
    }

    vehicle_create_trains(ride->id, x, y, z, tileElement);
    // return true;

    // Initialise station departs
    // 006DDDD0:
    ride->lifecycle_flags |= RIDE_LIFECYCLE_ON_TRACK;
    for (int32_t i = 0; i < MAX_STATIONS; i++)
    {
        ride->stations[i].Depart = (ride->stations[i].Depart & STATION_DEPART_FLAG) | 1;
    }

    //
    if (ride->type != RIDE_TYPE_SPACE_RINGS && !ride_type_has_flag(ride->type, RIDE_TYPE_FLAG_16))
    {
        if (ride->IsBlockSectioned())
        {
            CoordsXYE firstBlock;
            ride_create_vehicles_find_first_block(ride, &firstBlock);
            loc_6DDF9C(ride, firstBlock.element);
        }
        else
        {
            for (int32_t i = 0; i < ride->num_vehicles; i++)
            {
                rct_vehicle* vehicle = GET_VEHICLE(ride->vehicles[i]);

                rct_ride_entry_vehicle* vehicleEntry = vehicle_get_vehicle_entry(vehicle);

                if (!(vehicleEntry->flags & VEHICLE_ENTRY_FLAG_DODGEM_CAR_PLACEMENT))
                {
                    vehicle_update_track_motion(vehicle, nullptr);
                }

                vehicle_unset_update_flag_b1(vehicle);
            }
        }
    }
    ride_update_vehicle_colours(ride);
    return true;
}

/**
 *
 *  rct2: 0x006DDF9C
 */
void loc_6DDF9C(Ride* ride, TileElement* tileElement)
{
    rct_vehicle *train, *car;

    for (int32_t i = 0; i < ride->num_vehicles; i++)
    {
        uint16_t vehicleSpriteIdx = ride->vehicles[i];
        if (vehicleSpriteIdx == SPRITE_INDEX_NULL)
            continue;

        train = GET_VEHICLE(vehicleSpriteIdx);
        if (i == 0)
        {
            vehicle_update_track_motion(train, nullptr);
            vehicle_unset_update_flag_b1(train);
            continue;
        }

        vehicle_update_track_motion(train, nullptr);

        do
        {
            tileElement->AsTrack()->SetBlockBrakeClosed(true);
            car = train;
            while (true)
            {
                car->velocity = 0;
                car->acceleration = 0;
                car->swing_sprite = 0;
                car->remaining_distance += 13962;

                uint16_t spriteIndex = car->next_vehicle_on_train;
                if (spriteIndex == SPRITE_INDEX_NULL)
                {
                    break;
                }
                car = GET_VEHICLE(spriteIndex);
            }
        } while (!(vehicle_update_track_motion(train, nullptr) & VEHICLE_UPDATE_MOTION_TRACK_FLAG_10));

        tileElement->AsTrack()->SetBlockBrakeClosed(true);
        car = train;
        while (true)
        {
            car->update_flags &= ~VEHICLE_UPDATE_FLAG_1;
            car->SetState(VEHICLE_STATUS_TRAVELLING, car->sub_state);
            if ((car->track_type >> 2) == TRACK_ELEM_END_STATION)
            {
                car->SetState(VEHICLE_STATUS_MOVING_TO_END_OF_STATION, car->sub_state);
            }

            uint16_t spriteIndex = car->next_vehicle_on_train;
            if (spriteIndex == SPRITE_INDEX_NULL)
            {
                break;
            }
            car = GET_VEHICLE(spriteIndex);
        }
    }
}

/**
 * Checks and initialises the cable lift track returns false if unable to find
 * appropriate track.
 *  rct2: 0x006D31A6
 */
static bool ride_initialise_cable_lift_track(Ride* ride, bool isApplying)
{
    TileCoordsXY location;
    int32_t stationIndex;
    for (stationIndex = 0; stationIndex < MAX_STATIONS; stationIndex++)
    {
        location = ride->stations[stationIndex].Start;
        if (!location.isNull())
            break;
        if (stationIndex == (MAX_STATIONS - 1))
        {
            gGameCommandErrorText = STR_CABLE_LIFT_HILL_MUST_START_IMMEDIATELY_AFTER_STATION;
            return false;
        }
    }

    int32_t x = location.x * 32;
    int32_t y = location.y * 32;
    int32_t z = ride->stations[stationIndex].GetBaseZ();

    bool success = false;
    TileElement* tileElement = map_get_first_element_at({ x, y });
    if (tileElement == nullptr)
        return success;
    do
    {
        if (tileElement->GetType() != TILE_ELEMENT_TYPE_TRACK)
            continue;
        if (tileElement->GetBaseZ() != z)
            continue;

        if (!(TrackSequenceProperties[tileElement->AsTrack()->GetTrackType()][0] & TRACK_SEQUENCE_FLAG_ORIGIN))
        {
            continue;
        }
        success = true;
        break;
    } while (!(tileElement++)->IsLastForTile());

    if (!success)
        return false;

    enum
    {
        STATE_FIND_CABLE_LIFT,
        STATE_FIND_STATION,
        STATE_REST_OF_TRACK
    };
    int32_t state = STATE_FIND_CABLE_LIFT;

    track_circuit_iterator it;
    track_circuit_iterator_begin(&it, { x, y, tileElement });
    while (track_circuit_iterator_previous(&it))
    {
        tileElement = it.current.element;
        int32_t trackType = tileElement->AsTrack()->GetTrackType();

        uint16_t flags = 16;
        switch (state)
        {
            case STATE_FIND_CABLE_LIFT:
                // Search for a cable lift hill track element
                if (trackType == TRACK_ELEM_CABLE_LIFT_HILL)
                {
                    flags = 8;
                    state = STATE_FIND_STATION;
                }
                break;
            case STATE_FIND_STATION:
                // Search for the start of the hill
                switch (trackType)
                {
                    case TRACK_ELEM_FLAT:
                    case TRACK_ELEM_25_DEG_UP:
                    case TRACK_ELEM_60_DEG_UP:
                    case TRACK_ELEM_FLAT_TO_25_DEG_UP:
                    case TRACK_ELEM_25_DEG_UP_TO_FLAT:
                    case TRACK_ELEM_25_DEG_UP_TO_60_DEG_UP:
                    case TRACK_ELEM_60_DEG_UP_TO_25_DEG_UP:
                    case TRACK_ELEM_FLAT_TO_60_DEG_UP_LONG_BASE:
                        flags = 8;
                        break;
                    case TRACK_ELEM_END_STATION:
                        state = STATE_REST_OF_TRACK;
                        break;
                    default:
                        gGameCommandErrorText = STR_CABLE_LIFT_HILL_MUST_START_IMMEDIATELY_AFTER_STATION;
                        return false;
                }
                break;
        }
        if (isApplying)
        {
            z = tileElement->GetBaseZ();
            int32_t direction = tileElement->GetDirection();
            trackType = tileElement->AsTrack()->GetTrackType();
            x = it.current.x;
            y = it.current.y;
            sub_6C683D(&x, &y, &z, direction, trackType, 0, &tileElement, flags);
        }
    }
    return true;
}

/**
 *
 *  rct2: 0x006DF4D4
 */
static bool ride_create_cable_lift(ride_id_t rideIndex, bool isApplying)
{
    auto ride = get_ride(rideIndex);
    if (ride == nullptr)
        return false;

    if (ride->mode != RIDE_MODE_CONTINUOUS_CIRCUIT_BLOCK_SECTIONED && ride->mode != RIDE_MODE_CONTINUOUS_CIRCUIT)
    {
        gGameCommandErrorText = STR_CABLE_LIFT_UNABLE_TO_WORK_IN_THIS_OPERATING_MODE;
        return false;
    }

    if (ride->num_circuits > 1)
    {
        gGameCommandErrorText = STR_MULTICIRCUIT_NOT_POSSIBLE_WITH_CABLE_LIFT_HILL;
        return false;
    }

    if (count_free_misc_sprite_slots() <= 5)
    {
        gGameCommandErrorText = STR_UNABLE_TO_CREATE_ENOUGH_VEHICLES;
        return false;
    }

    if (!ride_initialise_cable_lift_track(ride, isApplying))
    {
        return false;
    }

    if (!isApplying)
    {
        return true;
    }

    auto cableLiftLoc = ride->CableLiftLoc;
    auto tileElement = map_get_track_element_at(cableLiftLoc);
    int32_t direction = tileElement->GetDirection();

    rct_vehicle* head = nullptr;
    rct_vehicle* tail = nullptr;
    uint32_t ebx = 0;
    for (int32_t i = 0; i < 5; i++)
    {
        uint32_t edx = ror32(0x15478, 10);
        uint16_t var_44 = edx & 0xFFFF;
        edx = rol32(edx, 10) >> 1;
        ebx -= edx;
        int32_t remaining_distance = ebx;
        ebx -= edx;

        rct_vehicle* current = cable_lift_segment_create(
            *ride, cableLiftLoc.x, cableLiftLoc.y, cableLiftLoc.z / 8, direction, var_44, remaining_distance, i == 0);
        current->next_vehicle_on_train = SPRITE_INDEX_NULL;
        if (i == 0)
        {
            head = current;
        }
        else
        {
            tail->next_vehicle_on_train = current->sprite_index;
            tail->next_vehicle_on_ride = current->sprite_index;
            current->prev_vehicle_on_ride = tail->sprite_index;
        }
        tail = current;
    }
    head->prev_vehicle_on_ride = tail->sprite_index;
    tail->next_vehicle_on_ride = head->sprite_index;

    ride->lifecycle_flags |= RIDE_LIFECYCLE_CABLE_LIFT;
    cable_lift_update_track_motion(head);
    return true;
}

/**
 *
 *  rct2: 0x006B51C0
 */
static void loc_6B51C0(const Ride* ride)
{
    rct_window* w = window_get_main();
    if (w == nullptr)
        return;

    int8_t entranceOrExit = -1;
    int32_t i;
    for (i = 0; i < MAX_STATIONS; i++)
    {
        if (ride->stations[i].Start.isNull())
            continue;

        if (ride_get_entrance_location(ride, i).isNull())
        {
            entranceOrExit = 0;
            break;
        }

        if (ride_get_exit_location(ride, i).isNull())
        {
            entranceOrExit = 1;
            break;
        }
    }

    if (entranceOrExit == -1)
        return;

    if (ride->type != RIDE_TYPE_MAZE)
    {
        int32_t x = ride->stations[i].Start.x * 32;
        int32_t y = ride->stations[i].Start.y * 32;
        int32_t z = ride->stations[i].GetBaseZ();
        window_scroll_to_location(w, x, y, z);

        CoordsXYE trackElement;
        ride_try_get_origin_element(ride, &trackElement);
        ride_find_track_gap(ride, &trackElement, &trackElement);
        int32_t ok = ride_modify(&trackElement);
        if (ok == 0)
        {
            return;
        }

        w = window_find_by_class(WC_RIDE_CONSTRUCTION);
        if (w != nullptr)
            window_event_mouse_up_call(w, WC_RIDE_CONSTRUCTION__WIDX_ENTRANCE + entranceOrExit);
    }
}

/**
 *
 *  rct2: 0x006B528A
 */
static void ride_scroll_to_track_error(CoordsXYE* trackElement)
{
    rct_window* w = window_get_main();
    if (w != nullptr)
    {
        window_scroll_to_location(w, trackElement->x, trackElement->y, trackElement->element->GetBaseZ());
        ride_modify(trackElement);
    }
}

/**
 *
 *  rct2: 0x006B4F6B
 */
static TileElement* loc_6B4F6B(ride_id_t rideIndex, int32_t x, int32_t y)
{
    auto ride = get_ride(rideIndex);
    if (ride == nullptr)
        return nullptr;

    TileElement* tileElement = map_get_first_element_at({ x, y });
    if (tileElement == nullptr)
        return nullptr;
    do
    {
        if (tileElement->GetType() != TILE_ELEMENT_TYPE_TRACK)
            continue;

        if (RideProperties[ride->type].flags & RIDE_TYPE_FLAG_FLAT_RIDE)
        {
            if (!(FlatRideTrackSequenceProperties[tileElement->AsTrack()->GetTrackType()][0] & TRACK_SEQUENCE_FLAG_ORIGIN))
                continue;
        }
        else
        {
            if (!(TrackSequenceProperties[tileElement->AsTrack()->GetTrackType()][0] & TRACK_SEQUENCE_FLAG_ORIGIN))
                continue;
        }

        if (tileElement->AsTrack()->GetRideIndex() == rideIndex)
            return tileElement;
    } while (!(tileElement++)->IsLastForTile());

    return nullptr;
}

int32_t ride_is_valid_for_test(Ride* ride, int32_t status, bool isApplying)
{
    int32_t stationIndex;
    CoordsXYE trackElement, problematicTrackElement = {};

    if (ride->type == RIDE_TYPE_NULL)
    {
        log_warning("Invalid ride type for ride %u", ride->id);
        return 0;
    }

    if (status != RIDE_STATUS_SIMULATING)
    {
        window_close_by_number(WC_RIDE_CONSTRUCTION, ride->id);
    }

    stationIndex = ride_mode_check_station_present(ride);
    if (stationIndex == -1)
        return 0;

    if (!ride_mode_check_valid_station_numbers(ride))
        return 0;

    if (status != RIDE_STATUS_SIMULATING && !ride_check_for_entrance_exit(ride->id))
    {
        loc_6B51C0(ride);
        return 0;
    }

    if (status == RIDE_STATUS_OPEN && isApplying)
    {
        sub_6B5952(ride);
        ride->lifecycle_flags |= RIDE_LIFECYCLE_EVER_BEEN_OPENED;
    }

    // z = ride->stations[i].GetBaseZ();
    trackElement.x = ride->stations[stationIndex].Start.x * 32;
    trackElement.y = ride->stations[stationIndex].Start.y * 32;
    trackElement.element = loc_6B4F6B(ride->id, trackElement.x, trackElement.y);
    if (trackElement.element == nullptr)
    {
        // Maze is strange, station start is 0... investigation required
        if (ride->type != RIDE_TYPE_MAZE)
            return 0;
    }

    if (ride->type == RIDE_TYPE_AIR_POWERED_VERTICAL_COASTER || ride->mode == RIDE_MODE_CONTINUOUS_CIRCUIT
        || ride->mode == RIDE_MODE_CONTINUOUS_CIRCUIT_BLOCK_SECTIONED || ride->mode == RIDE_MODE_POWERED_LAUNCH_BLOCK_SECTIONED)
    {
        if (ride_find_track_gap(ride, &trackElement, &problematicTrackElement)
            && (status != RIDE_STATUS_SIMULATING || ride->mode == RIDE_MODE_CONTINUOUS_CIRCUIT_BLOCK_SECTIONED
                || ride->mode == RIDE_MODE_POWERED_LAUNCH_BLOCK_SECTIONED))
        {
            gGameCommandErrorText = STR_TRACK_IS_NOT_A_COMPLETE_CIRCUIT;
            ride_scroll_to_track_error(&problematicTrackElement);
            return 0;
        }
    }

    if (ride->mode == RIDE_MODE_CONTINUOUS_CIRCUIT_BLOCK_SECTIONED || ride->mode == RIDE_MODE_POWERED_LAUNCH_BLOCK_SECTIONED)
    {
        if (!ride_check_block_brakes(&trackElement, &problematicTrackElement))
        {
            ride_scroll_to_track_error(&problematicTrackElement);
            return 0;
        }
    }

    if (ride->subtype != RIDE_ENTRY_INDEX_NULL)
    {
        rct_ride_entry* rideType = get_ride_entry(ride->subtype);
        if (rideType->flags & RIDE_ENTRY_FLAG_NO_INVERSIONS)
        {
            gGameCommandErrorText = STR_TRACK_UNSUITABLE_FOR_TYPE_OF_TRAIN;
            if (ride_check_track_contains_inversions(&trackElement, &problematicTrackElement))
            {
                ride_scroll_to_track_error(&problematicTrackElement);
                return 0;
            }
        }
        if (rideType->flags & RIDE_ENTRY_FLAG_NO_BANKED_TRACK)
        {
            gGameCommandErrorText = STR_TRACK_UNSUITABLE_FOR_TYPE_OF_TRAIN;
            if (ride_check_track_contains_banked(&trackElement, &problematicTrackElement))
            {
                ride_scroll_to_track_error(&problematicTrackElement);
                return 0;
            }
        }
    }

    if (ride->mode == RIDE_MODE_STATION_TO_STATION)
    {
        if (!ride_find_track_gap(ride, &trackElement, &problematicTrackElement))
        {
            gGameCommandErrorText = STR_RIDE_MUST_START_AND_END_WITH_STATIONS;
            return 0;
        }

        gGameCommandErrorText = STR_STATION_NOT_LONG_ENOUGH;
        if (!ride_check_station_length(&trackElement, &problematicTrackElement))
        {
            ride_scroll_to_track_error(&problematicTrackElement);
            return 0;
        }

        gGameCommandErrorText = STR_RIDE_MUST_START_AND_END_WITH_STATIONS;
        if (!ride_check_start_and_end_is_station(&trackElement))
        {
            ride_scroll_to_track_error(&problematicTrackElement);
            return 0;
        }
    }

    if (isApplying)
        ride_set_start_finish_points(ride->id, &trackElement);

    if (!ride_type_has_flag(ride->type, RIDE_TYPE_FLAG_NO_VEHICLES) && !(ride->lifecycle_flags & RIDE_LIFECYCLE_ON_TRACK))
    {
        if (!ride_create_vehicles(ride, &trackElement, isApplying))
        {
            return 0;
        }
    }

    if ((RideData4[ride->type].flags & RIDE_TYPE_FLAG4_ALLOW_CABLE_LIFT_HILL)
        && (ride->lifecycle_flags & RIDE_LIFECYCLE_CABLE_LIFT_HILL_COMPONENT_USED)
        && !(ride->lifecycle_flags & RIDE_LIFECYCLE_CABLE_LIFT))
    {
        if (!ride_create_cable_lift(ride->id, isApplying))
            return 0;
    }

    return 1;
}
/**
 *
 *  rct2: 0x006B4EEA
 */
int32_t ride_is_valid_for_open(Ride* ride, int32_t goingToBeOpen, bool isApplying)
{
    int32_t stationIndex;
    CoordsXYE trackElement, problematicTrackElement = {};

    // Check to see if construction tool is in use. If it is close the construction window
    // to set the track to its final state and clean up ghosts.
    // We can't just call close as it would cause a stack overflow during shop creation
    // with auto open on.
    if (WC_RIDE_CONSTRUCTION == gCurrentToolWidget.window_classification && ride->id == gCurrentToolWidget.window_number
        && (input_test_flag(INPUT_FLAG_TOOL_ACTIVE)))
        window_close_by_number(WC_RIDE_CONSTRUCTION, ride->id);

    stationIndex = ride_mode_check_station_present(ride);
    if (stationIndex == -1)
        return 0;

    if (!ride_mode_check_valid_station_numbers(ride))
        return 0;

    if (!ride_check_for_entrance_exit(ride->id))
    {
        loc_6B51C0(ride);
        return 0;
    }

    if (goingToBeOpen && isApplying)
    {
        sub_6B5952(ride);
        ride->lifecycle_flags |= RIDE_LIFECYCLE_EVER_BEEN_OPENED;
    }

    // z = ride->stations[i].GetBaseZ();
    trackElement.x = ride->stations[stationIndex].Start.x * 32;
    trackElement.y = ride->stations[stationIndex].Start.y * 32;
    trackElement.element = loc_6B4F6B(ride->id, trackElement.x, trackElement.y);
    if (trackElement.element == nullptr)
    {
        // Maze is strange, station start is 0... investigation required
        if (ride->type != RIDE_TYPE_MAZE)
            return 0;
    }

    if (ride->type == RIDE_TYPE_AIR_POWERED_VERTICAL_COASTER || ride->mode == RIDE_MODE_RACE
        || ride->mode == RIDE_MODE_CONTINUOUS_CIRCUIT || ride->mode == RIDE_MODE_CONTINUOUS_CIRCUIT_BLOCK_SECTIONED
        || ride->mode == RIDE_MODE_POWERED_LAUNCH_BLOCK_SECTIONED)
    {
        if (ride_find_track_gap(ride, &trackElement, &problematicTrackElement))
        {
            gGameCommandErrorText = STR_TRACK_IS_NOT_A_COMPLETE_CIRCUIT;
            ride_scroll_to_track_error(&problematicTrackElement);
            return 0;
        }
    }

    if (ride->mode == RIDE_MODE_CONTINUOUS_CIRCUIT_BLOCK_SECTIONED || ride->mode == RIDE_MODE_POWERED_LAUNCH_BLOCK_SECTIONED)
    {
        if (!ride_check_block_brakes(&trackElement, &problematicTrackElement))
        {
            ride_scroll_to_track_error(&problematicTrackElement);
            return 0;
        }
    }

    if (ride->subtype != RIDE_ENTRY_INDEX_NULL)
    {
        rct_ride_entry* rideType = get_ride_entry(ride->subtype);
        if (rideType->flags & RIDE_ENTRY_FLAG_NO_INVERSIONS)
        {
            gGameCommandErrorText = STR_TRACK_UNSUITABLE_FOR_TYPE_OF_TRAIN;
            if (ride_check_track_contains_inversions(&trackElement, &problematicTrackElement))
            {
                ride_scroll_to_track_error(&problematicTrackElement);
                return 0;
            }
        }
        if (rideType->flags & RIDE_ENTRY_FLAG_NO_BANKED_TRACK)
        {
            gGameCommandErrorText = STR_TRACK_UNSUITABLE_FOR_TYPE_OF_TRAIN;
            if (ride_check_track_contains_banked(&trackElement, &problematicTrackElement))
            {
                ride_scroll_to_track_error(&problematicTrackElement);
                return 0;
            }
        }
    }

    if (ride->mode == RIDE_MODE_STATION_TO_STATION)
    {
        if (!ride_find_track_gap(ride, &trackElement, &problematicTrackElement))
        {
            gGameCommandErrorText = STR_RIDE_MUST_START_AND_END_WITH_STATIONS;
            return 0;
        }

        gGameCommandErrorText = STR_STATION_NOT_LONG_ENOUGH;
        if (!ride_check_station_length(&trackElement, &problematicTrackElement))
        {
            ride_scroll_to_track_error(&problematicTrackElement);
            return 0;
        }

        gGameCommandErrorText = STR_RIDE_MUST_START_AND_END_WITH_STATIONS;
        if (!ride_check_start_and_end_is_station(&trackElement))
        {
            ride_scroll_to_track_error(&problematicTrackElement);
            return 0;
        }
    }

    if (isApplying)
        ride_set_start_finish_points(ride->id, &trackElement);

    if (!ride_type_has_flag(ride->type, RIDE_TYPE_FLAG_NO_VEHICLES) && !(ride->lifecycle_flags & RIDE_LIFECYCLE_ON_TRACK))
    {
        if (!ride_create_vehicles(ride, &trackElement, isApplying))
        {
            return 0;
        }
    }

    if ((RideData4[ride->type].flags & RIDE_TYPE_FLAG4_ALLOW_CABLE_LIFT_HILL)
        && (ride->lifecycle_flags & RIDE_LIFECYCLE_CABLE_LIFT_HILL_COMPONENT_USED)
        && !(ride->lifecycle_flags & RIDE_LIFECYCLE_CABLE_LIFT))
    {
        if (!ride_create_cable_lift(ride->id, isApplying))
            return 0;
    }

    return 1;
}

/**
 * Given a track element of the ride, find the start of the track.
 * It has to do this as a backwards loop in case this is an incomplete track.
 */
void ride_get_start_of_track(CoordsXYE* output)
{
    track_begin_end trackBeginEnd;
    CoordsXYE trackElement = *output;
    if (track_block_get_previous(trackElement.x, trackElement.y, trackElement.element, &trackBeginEnd))
    {
        TileElement* initial_map = trackElement.element;
        track_begin_end slowIt = trackBeginEnd;
        bool moveSlowIt = true;
        do
        {
            CoordsXYE lastGood = {
                /* .x = */ trackBeginEnd.begin_x,
                /* .y = */ trackBeginEnd.begin_y,
                /* .element = */ trackBeginEnd.begin_element,
            };

            if (!track_block_get_previous(
                    trackBeginEnd.end_x, trackBeginEnd.end_y, trackBeginEnd.begin_element, &trackBeginEnd))
            {
                trackElement = lastGood;
                break;
            }

            moveSlowIt = !moveSlowIt;
            if (moveSlowIt)
            {
                if (!track_block_get_previous(slowIt.end_x, slowIt.end_y, slowIt.begin_element, &slowIt)
                    || slowIt.begin_element == trackBeginEnd.begin_element)
                {
                    break;
                }
            }
        } while (initial_map != trackBeginEnd.begin_element);
    }
    *output = trackElement;
}

/**
 *
 *  rct2: 0x006CB7FB
 */
int32_t ride_get_refund_price(const Ride* ride)
{
    CoordsXYE trackElement;
    money32 cost = 0;

    if (!ride_try_get_origin_element(ride, &trackElement))
    {
        return 0; // Ride has no track to refund
    }

    // Find the start in case it is not a complete circuit
    ride_get_start_of_track(&trackElement);

    uint8_t direction = trackElement.element->GetDirection();

    // Used in the following loop to know when we have
    // completed all of the elements and are back at the
    // start.
    TileElement* initial_map = trackElement.element;
    CoordsXYE slowIt = trackElement;
    bool moveSlowIt = true;

    do
    {
        auto trackRemoveAction = TrackRemoveAction(
            trackElement.element->AsTrack()->GetTrackType(), trackElement.element->AsTrack()->GetSequenceIndex(),
            { trackElement.x, trackElement.y, trackElement.element->GetBaseZ(), direction });
        trackRemoveAction.SetFlags(GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED);

        auto res = GameActions::Query(&trackRemoveAction);

        cost += res->Cost;

        if (!track_block_get_next(&trackElement, &trackElement, nullptr, nullptr))
        {
            break;
        }

        moveSlowIt = !moveSlowIt;
        if (moveSlowIt)
        {
            if (!track_block_get_next(&slowIt, &slowIt, nullptr, nullptr) || slowIt.element == trackElement.element)
            {
                break;
            }
        }

        direction = trackElement.element->GetDirection();

    } while (trackElement.element != initial_map);

    return cost;
}

/**
 *
 *  rct2: 0x00696707
 */
void Ride::StopGuestsQueuing()
{
    uint16_t spriteIndex;
    Peep* peep;

    FOR_ALL_PEEPS (spriteIndex, peep)
    {
        if (peep->state != PEEP_STATE_QUEUING)
            continue;
        if (peep->current_ride != id)
            continue;

        peep->RemoveFromQueue();
        peep->SetState(PEEP_STATE_FALLING);
    }
}

uint8_t Ride::GetDefaultMode() const
{
    const rct_ride_entry* rideEntry = get_ride_entry(subtype);
    const uint8_t* availableModes = RideAvailableModes;

    for (int32_t i = 0; i < type; i++)
    {
        while (*(availableModes++) != RIDE_MODE_NULL)
        {
        }
    }
    // Since this only selects a default mode and does not prevent other modes from being used, there is no need
    // to check if select-by-track-type or the all-ride-modes cheat have been enabled.
    if (rideEntry->flags & RIDE_ENTRY_DISABLE_FIRST_TWO_OPERATING_MODES)
    {
        availableModes += 2;
    }
    return availableModes[0];
}

static bool ride_with_colour_config_exists(uint8_t ride_type, const TrackColour* colours)
{
    for (auto& ride : GetRideManager())
    {
        if (ride.type != ride_type)
            continue;
        if (ride.track_colour[0].main != colours->main)
            continue;
        if (ride.track_colour[0].additional != colours->additional)
            continue;
        if (ride.track_colour[0].supports != colours->supports)
            continue;

        return true;
    }
    return false;
}

bool Ride::NameExists(const std::string_view& name, ride_id_t excludeRideId)
{
    char buffer[256]{};
    uint32_t formatArgs[32]{};

    for (auto& ride : GetRideManager())
    {
        if (ride.id != excludeRideId)
        {
            ride.FormatNameTo(formatArgs);
            format_string(buffer, 256, STR_STRINGID, formatArgs);
            if (std::string_view(buffer) == name && ride_has_any_track_elements(&ride))
            {
                return true;
            }
        }
    }
    return false;
}

/**
 *
 *  Based on rct2: 0x006B4776
 */
int32_t ride_get_random_colour_preset_index(uint8_t ride_type)
{
    if (ride_type >= 128)
    {
        return 0;
    }

    const track_colour_preset_list* colourPresets = &RideColourPresets[ride_type];

    // 200 attempts to find a colour preset that hasn't already been used in the park for this ride type
    for (int32_t i = 0; i < 200; i++)
    {
        int32_t listIndex = util_rand() % colourPresets->count;
        const TrackColour* colours = &colourPresets->list[listIndex];

        if (!ride_with_colour_config_exists(ride_type, colours))
        {
            return listIndex;
        }
    }
    return 0;
}

/**
 *
 *  Based on rct2: 0x006B4776
 */
void Ride::SetColourPreset(uint8_t index)
{
    const track_colour_preset_list* colourPresets = &RideColourPresets[type];
    TrackColour colours = { COLOUR_BLACK, COLOUR_BLACK, COLOUR_BLACK };
    // Stalls save their default colour in the vehicle settings (since they share a common ride type)
    if (!IsRide())
    {
        auto rideEntry = get_ride_entry(subtype);
        if (rideEntry != nullptr && rideEntry->vehicle_preset_list->count > 0)
        {
            auto list = rideEntry->vehicle_preset_list->list[0];
            colours = { list.main, list.additional_1, list.additional_2 };
        }
    }
    else if (index < colourPresets->count)
    {
        colours = colourPresets->list[index];
    }
    for (int32_t i = 0; i < NUM_COLOUR_SCHEMES; i++)
    {
        track_colour[i].main = colours.main;
        track_colour[i].additional = colours.additional;
        track_colour[i].supports = colours.supports;
    }
    colour_scheme_type = 0;
}

money32 ride_get_common_price(Ride* forRide)
{
    for (const auto& ride : GetRideManager())
    {
        if (ride.type == forRide->type && &ride != forRide)
        {
            return ride.price;
        }
    }

    return MONEY32_UNDEFINED;
}

void Ride::SetNameToDefault()
{
    char rideNameBuffer[256]{};
    uint8_t rideNameArgs[32]{};

    // Increment default name number until we find a unique name
    custom_name = {};
    default_name_number = 0;
    do
    {
        default_name_number++;
        FormatNameTo(rideNameArgs);
        format_string(rideNameBuffer, 256, STR_STRINGID, &rideNameArgs);
    } while (Ride::NameExists(rideNameBuffer, id));
}

/**
 * This will return the name of the ride, as seen in the New Ride window.
 */
rct_ride_name get_ride_naming(const uint8_t rideType, rct_ride_entry* rideEntry)
{
    if (RideGroupManager::RideTypeHasRideGroups(rideType))
    {
        const RideGroup* rideGroup = RideGroupManager::GetRideGroup(rideType, rideEntry);
        return rideGroup->Naming;
    }
    else if (!RideGroupManager::RideTypeIsIndependent(rideType))
    {
        return RideNaming[rideType];
    }
    else
    {
        return rideEntry->naming;
    }
}

bool ride_type_has_flag(int32_t rideType, uint32_t flag)
{
    return (RideProperties[rideType].flags & flag) != 0;
}

/*
 * The next eight functions are helpers to access ride data at the offset 10E &
 * 110. Known as the turn counts. There are 3 different types (default, banked, sloped)
 * and there are 4 counts as follows:
 *
 * 1 element turns: low 5 bits
 * 2 element turns: bits 6-8
 * 3 element turns: bits 9-11
 * 4 element or more turns: bits 12-15
 *
 * 4 plus elements only possible on sloped type. Falls back to 3 element
 * if by some miracle you manage 4 element none sloped.
 */

void increment_turn_count_1_element(Ride* ride, uint8_t type)
{
    uint16_t* turn_count;
    switch (type)
    {
        case 0:
            turn_count = &ride->turn_count_default;
            break;
        case 1:
            turn_count = &ride->turn_count_banked;
            break;
        case 2:
            turn_count = &ride->turn_count_sloped;
            break;
        default:
            return;
    }
    uint16_t value = (*turn_count & TURN_MASK_1_ELEMENT) + 1;
    *turn_count &= ~TURN_MASK_1_ELEMENT;

    if (value > TURN_MASK_1_ELEMENT)
        value = TURN_MASK_1_ELEMENT;
    *turn_count |= value;
}

void increment_turn_count_2_elements(Ride* ride, uint8_t type)
{
    uint16_t* turn_count;
    switch (type)
    {
        case 0:
            turn_count = &ride->turn_count_default;
            break;
        case 1:
            turn_count = &ride->turn_count_banked;
            break;
        case 2:
            turn_count = &ride->turn_count_sloped;
            break;
        default:
            return;
    }
    uint16_t value = (*turn_count & TURN_MASK_2_ELEMENTS) + 0x20;
    *turn_count &= ~TURN_MASK_2_ELEMENTS;

    if (value > TURN_MASK_2_ELEMENTS)
        value = TURN_MASK_2_ELEMENTS;
    *turn_count |= value;
}

void increment_turn_count_3_elements(Ride* ride, uint8_t type)
{
    uint16_t* turn_count;
    switch (type)
    {
        case 0:
            turn_count = &ride->turn_count_default;
            break;
        case 1:
            turn_count = &ride->turn_count_banked;
            break;
        case 2:
            turn_count = &ride->turn_count_sloped;
            break;
        default:
            return;
    }
    uint16_t value = (*turn_count & TURN_MASK_3_ELEMENTS) + 0x100;
    *turn_count &= ~TURN_MASK_3_ELEMENTS;

    if (value > TURN_MASK_3_ELEMENTS)
        value = TURN_MASK_3_ELEMENTS;
    *turn_count |= value;
}

void increment_turn_count_4_plus_elements(Ride* ride, uint8_t type)
{
    uint16_t* turn_count;
    switch (type)
    {
        case 0:
        case 1:
            // Just in case fallback to 3 element turn
            increment_turn_count_3_elements(ride, type);
            return;
        case 2:
            turn_count = &ride->turn_count_sloped;
            break;
        default:
            return;
    }
    uint16_t value = (*turn_count & TURN_MASK_4_PLUS_ELEMENTS) + 0x800;
    *turn_count &= ~TURN_MASK_4_PLUS_ELEMENTS;

    if (value > TURN_MASK_4_PLUS_ELEMENTS)
        value = TURN_MASK_4_PLUS_ELEMENTS;
    *turn_count |= value;
}

int32_t get_turn_count_1_element(Ride* ride, uint8_t type)
{
    uint16_t* turn_count;
    switch (type)
    {
        case 0:
            turn_count = &ride->turn_count_default;
            break;
        case 1:
            turn_count = &ride->turn_count_banked;
            break;
        case 2:
            turn_count = &ride->turn_count_sloped;
            break;
        default:
            return 0;
    }

    return (*turn_count) & TURN_MASK_1_ELEMENT;
}

int32_t get_turn_count_2_elements(Ride* ride, uint8_t type)
{
    uint16_t* turn_count;
    switch (type)
    {
        case 0:
            turn_count = &ride->turn_count_default;
            break;
        case 1:
            turn_count = &ride->turn_count_banked;
            break;
        case 2:
            turn_count = &ride->turn_count_sloped;
            break;
        default:
            return 0;
    }

    return ((*turn_count) & TURN_MASK_2_ELEMENTS) >> 5;
}

int32_t get_turn_count_3_elements(Ride* ride, uint8_t type)
{
    uint16_t* turn_count;
    switch (type)
    {
        case 0:
            turn_count = &ride->turn_count_default;
            break;
        case 1:
            turn_count = &ride->turn_count_banked;
            break;
        case 2:
            turn_count = &ride->turn_count_sloped;
            break;
        default:
            return 0;
    }

    return ((*turn_count) & TURN_MASK_3_ELEMENTS) >> 8;
}

int32_t get_turn_count_4_plus_elements(Ride* ride, uint8_t type)
{
    uint16_t* turn_count;
    switch (type)
    {
        case 0:
        case 1:
            return 0;
        case 2:
            turn_count = &ride->turn_count_sloped;
            break;
        default:
            return 0;
    }

    return ((*turn_count) & TURN_MASK_4_PLUS_ELEMENTS) >> 11;
}

bool Ride::HasSpinningTunnel() const
{
    return special_track_elements & RIDE_ELEMENT_TUNNEL_SPLASH_OR_RAPIDS;
}

bool Ride::HasWaterSplash() const
{
    return special_track_elements & RIDE_ELEMENT_TUNNEL_SPLASH_OR_RAPIDS;
}

bool Ride::HasRapids() const
{
    return special_track_elements & RIDE_ELEMENT_TUNNEL_SPLASH_OR_RAPIDS;
}

bool Ride::HasLogReverser() const
{
    return special_track_elements & RIDE_ELEMENT_REVERSER_OR_WATERFALL;
}

bool Ride::HasWaterfall() const
{
    return special_track_elements & RIDE_ELEMENT_REVERSER_OR_WATERFALL;
}

bool Ride::HasWhirlpool() const
{
    return special_track_elements & RIDE_ELEMENT_WHIRLPOOL;
}

uint8_t ride_get_helix_sections(Ride* ride)
{
    // Helix sections stored in the low 5 bits.
    return ride->special_track_elements & 0x1F;
}

bool Ride::IsPoweredLaunched() const
{
    return mode == RIDE_MODE_POWERED_LAUNCH_PASSTROUGH || mode == RIDE_MODE_POWERED_LAUNCH
        || mode == RIDE_MODE_POWERED_LAUNCH_BLOCK_SECTIONED;
}

bool Ride::IsBlockSectioned() const
{
    return mode == RIDE_MODE_CONTINUOUS_CIRCUIT_BLOCK_SECTIONED || mode == RIDE_MODE_POWERED_LAUNCH_BLOCK_SECTIONED;
}

bool ride_has_any_track_elements(const Ride* ride)
{
    tile_element_iterator it;

    tile_element_iterator_begin(&it);
    while (tile_element_iterator_next(&it))
    {
        if (it.element->GetType() != TILE_ELEMENT_TYPE_TRACK)
            continue;
        if (it.element->AsTrack()->GetRideIndex() != ride->id)
            continue;
        if (it.element->IsGhost())
            continue;

        return true;
    }

    return false;
}

/**
 *
 *  rct2: 0x006847BA
 */
void set_vehicle_type_image_max_sizes(rct_ride_entry_vehicle* vehicle_type, int32_t num_images)
{
    uint8_t bitmap[200][200] = { 0 };

    rct_drawpixelinfo dpi = {
        /*.bits = */ (uint8_t*)bitmap,
        /*.x = */ -100,
        /*.y = */ -100,
        /*.width = */ 200,
        /*.height = */ 200,
        /*.pitch = */ 0,
        /*.zoom_level = */ 0,
    };

    for (int32_t i = 0; i < num_images; ++i)
    {
        gfx_draw_sprite_software(&dpi, ImageId::FromUInt32(vehicle_type->base_image_id + i), 0, 0);
    }
    int32_t al = -1;
    for (int32_t i = 99; i != 0; --i)
    {
        for (int32_t j = 0; j < 200; j++)
        {
            if (bitmap[j][100 - i] != 0)
            {
                al = i;
                break;
            }
        }

        if (al != -1)
            break;

        for (int32_t j = 0; j < 200; j++)
        {
            if (bitmap[j][100 + i] != 0)
            {
                al = i;
                break;
            }
        }

        if (al != -1)
            break;
    }

    al++;
    int32_t bl = -1;

    for (int32_t i = 99; i != 0; --i)
    {
        for (int32_t j = 0; j < 200; j++)
        {
            if (bitmap[100 - i][j] != 0)
            {
                bl = i;
                break;
            }
        }

        if (bl != -1)
            break;
    }
    bl++;

    int32_t bh = -1;

    for (int32_t i = 99; i != 0; --i)
    {
        for (int32_t j = 0; j < 200; j++)
        {
            if (bitmap[100 + i][j] != 0)
            {
                bh = i;
                break;
            }
        }

        if (bh != -1)
            break;
    }
    bh++;

    // Moved from object paint

    if (vehicle_type->flags & VEHICLE_ENTRY_FLAG_13)
    {
        bl += 16;
    }

    vehicle_type->sprite_width = al;
    vehicle_type->sprite_height_negative = bl;
    vehicle_type->sprite_height_positive = bh;
}

static int32_t loc_6CD18E(
    int16_t mapX, int16_t mapY, int16_t entranceMinX, int16_t entranceMinY, int16_t entranceMaxX, int16_t entranceMaxY)
{
    int32_t direction = 0;
    if (mapX == entranceMinX)
    {
        if (mapY > entranceMinY && mapY < entranceMaxY)
        {
            return direction;
        }
    }
    direction = 1;
    if (mapY == entranceMaxY)
    {
        if (mapX > entranceMinX && mapX < entranceMaxX)
        {
            return direction;
        }
    }
    direction = 2;
    if (mapX == entranceMaxX)
    {
        if (mapY > entranceMinY && mapY < entranceMaxY)
        {
            return direction;
        }
    }
    direction = 3;
    if (mapY == entranceMinY)
    {
        if (mapX > entranceMinX && mapX < entranceMaxX)
        {
            return direction;
        }
    }
    return -1;
}

/**
 *
 *  rct2: 0x006CCF70
 */
CoordsXYZD ride_get_entrance_or_exit_position_from_screen_position(ScreenCoordsXY screenCoords)
{
    int16_t entranceMinX, entranceMinY, entranceMaxX, entranceMaxY, word_F4418C, word_F4418E;
    int32_t interactionType, stationDirection;
    TileElement* tileElement;
    rct_viewport* viewport;
    Ride* ride;
    CoordsXYZD entranceExitCoords{};

    gRideEntranceExitPlaceDirection = 255;
    CoordsXY unusedCoords;
    get_map_coordinates_from_pos(screenCoords, 0xFFFB, unusedCoords, &interactionType, &tileElement, &viewport);
    if (interactionType != 0)
    {
        if (tileElement->GetType() == TILE_ELEMENT_TYPE_TRACK)
        {
            if (tileElement->AsTrack()->GetRideIndex() == gRideEntranceExitPlaceRideIndex)
            {
                if (TrackSequenceProperties[tileElement->AsTrack()->GetTrackType()][0] & TRACK_SEQUENCE_FLAG_ORIGIN)
                {
                    if (tileElement->AsTrack()->GetTrackType() == TRACK_ELEM_MAZE)
                    {
                        gRideEntranceExitPlaceStationIndex = 0;
                    }
                    else
                    {
                        gRideEntranceExitPlaceStationIndex = tileElement->AsTrack()->GetStationIndex();
                    }
                }
            }
        }
    }

    ride = get_ride(gRideEntranceExitPlaceRideIndex);
    if (ride == nullptr)
    {
        entranceExitCoords.setNull();
        return entranceExitCoords;
    }

    auto stationBaseZ = ride->stations[gRideEntranceExitPlaceStationIndex].GetBaseZ();

    auto coords = screen_get_map_xy_with_z(screenCoords, stationBaseZ);
    if (!coords)
    {
        entranceExitCoords.setNull();
        return entranceExitCoords;
    }

    word_F4418C = coords->x;
    word_F4418E = coords->y;

    entranceExitCoords = { coords->ToTileStart(), stationBaseZ, INVALID_DIRECTION };

    if (ride->type == RIDE_TYPE_NULL)
    {
        entranceExitCoords.setNull();
        return entranceExitCoords;
    }

    auto stationStart = ride->stations[gRideEntranceExitPlaceStationIndex].Start;
    if (stationStart.isNull())
    {
        entranceExitCoords.setNull();
        return entranceExitCoords;
    }

    if (ride_type_has_flag(ride->type, RIDE_TYPE_FLAG_3))
    {
        auto mapX = (word_F4418C & 0x1F) - 16;
        auto mapY = (word_F4418E & 0x1F) - 16;
        if (std::abs(mapX) < std::abs(mapY))
        {
            entranceExitCoords.direction = mapY < 0 ? 3 : 1;
        }
        else
        {
            entranceExitCoords.direction = mapX < 0 ? 0 : 2;
        }

        for (int32_t i = 0; i < MAX_STATIONS; i++)
        {
            mapX = entranceExitCoords.x + CoordsDirectionDelta[entranceExitCoords.direction].x;
            mapY = entranceExitCoords.y + CoordsDirectionDelta[entranceExitCoords.direction].y;
            if (map_is_location_valid({ mapX, mapY }))
            {
                tileElement = map_get_first_element_at({ mapX, mapY });
                if (tileElement == nullptr)
                    continue;
                do
                {
                    if (tileElement->GetType() != TILE_ELEMENT_TYPE_TRACK)
                        continue;
                    if (tileElement->GetBaseZ() != stationBaseZ)
                        continue;
                    if (tileElement->AsTrack()->GetRideIndex() != gRideEntranceExitPlaceRideIndex)
                        continue;
                    if (tileElement->AsTrack()->GetTrackType() == TRACK_ELEM_MAZE)
                    {
                        entranceExitCoords.direction = direction_reverse(entranceExitCoords.direction);
                        gRideEntranceExitPlaceDirection = entranceExitCoords.direction;
                        return entranceExitCoords;
                    }
                    if (tileElement->AsTrack()->GetStationIndex() != gRideEntranceExitPlaceStationIndex)
                        continue;

                    int32_t eax = (entranceExitCoords.direction + 2 - tileElement->GetDirection())
                        & TILE_ELEMENT_DIRECTION_MASK;
                    if (FlatRideTrackSequenceProperties[tileElement->AsTrack()->GetTrackType()]
                                                       [tileElement->AsTrack()->GetSequenceIndex()]
                        & (1 << eax))
                    {
                        entranceExitCoords.direction = direction_reverse(entranceExitCoords.direction);
                        gRideEntranceExitPlaceDirection = entranceExitCoords.direction;
                        return entranceExitCoords;
                    }
                } while (!(tileElement++)->IsLastForTile());
            }
            entranceExitCoords.direction = (entranceExitCoords.direction + 1) & 3;
        }
        gRideEntranceExitPlaceDirection = 0xFF;
    }
    else
    {
        auto mapX = stationStart.x * 32;
        auto mapY = stationStart.y * 32;
        entranceMinX = mapX;
        entranceMinY = mapY;

        tileElement = ride_get_station_start_track_element(ride, gRideEntranceExitPlaceStationIndex);
        if (tileElement == nullptr)
        {
            entranceExitCoords.setNull();
            return entranceExitCoords;
        }
        entranceExitCoords.direction = tileElement->GetDirection();
        stationDirection = entranceExitCoords.direction;

        while (true)
        {
            entranceMaxX = mapX;
            entranceMaxY = mapY;
            mapX -= CoordsDirectionDelta[entranceExitCoords.direction].x;
            mapY -= CoordsDirectionDelta[entranceExitCoords.direction].y;
            tileElement = map_get_first_element_at({ mapX, mapY });
            if (tileElement == nullptr)
                break;
            bool goToNextTile = false;

            do
            {
                if (tileElement->GetType() != TILE_ELEMENT_TYPE_TRACK)
                    continue;
                if (tileElement->AsTrack()->GetRideIndex() != gRideEntranceExitPlaceRideIndex)
                    continue;
                if (tileElement->AsTrack()->GetStationIndex() != gRideEntranceExitPlaceStationIndex)
                    continue;

                switch (tileElement->AsTrack()->GetTrackType())
                {
                    case TRACK_ELEM_END_STATION:
                    case TRACK_ELEM_BEGIN_STATION:
                    case TRACK_ELEM_MIDDLE_STATION:
                        goToNextTile = true;
                }
            } while (!goToNextTile && !(tileElement++)->IsLastForTile());

            if (!goToNextTile)
                break;
        }

        mapX = entranceMinX;
        if (mapX > entranceMaxX)
        {
            entranceMinX = entranceMaxX;
            entranceMaxX = mapX;
        }

        mapY = entranceMinY;
        if (mapY > entranceMaxY)
        {
            entranceMinY = entranceMaxY;
            entranceMaxY = mapY;
        }

        auto direction = loc_6CD18E(
            entranceExitCoords.x, entranceExitCoords.y, entranceMinX - 32, entranceMinY - 32, entranceMaxX + 32,
            entranceMaxY + 32);
        if (direction != -1 && direction != stationDirection && direction != direction_reverse(stationDirection))
        {
            entranceExitCoords.direction = direction;
            gRideEntranceExitPlaceDirection = entranceExitCoords.direction;
            return entranceExitCoords;
        }
    }
    return entranceExitCoords;
}

bool ride_select_backwards_from_front()
{
    auto ride = get_ride(_currentRideIndex);
    if (ride != nullptr)
    {
        ride_construction_invalidate_current_track();
        track_begin_end trackBeginEnd;
        if (track_block_get_previous_from_zero(
                _currentTrackBegin.x, _currentTrackBegin.y, _currentTrackBegin.z, ride, _currentTrackPieceDirection,
                &trackBeginEnd))
        {
            _rideConstructionState = RIDE_CONSTRUCTION_STATE_SELECTED;
            _currentTrackBegin.x = trackBeginEnd.begin_x;
            _currentTrackBegin.y = trackBeginEnd.begin_y;
            _currentTrackBegin.z = trackBeginEnd.begin_z;
            _currentTrackPieceDirection = trackBeginEnd.begin_direction;
            _currentTrackPieceType = trackBeginEnd.begin_element->AsTrack()->GetTrackType();
            _currentTrackSelectionFlags = 0;
            _rideConstructionArrowPulseTime = 0;
            return true;
        }
    }
    return false;
}

bool ride_select_forwards_from_back()
{
    auto ride = get_ride(_currentRideIndex);
    if (ride != nullptr)
    {
        ride_construction_invalidate_current_track();

        int32_t x = _currentTrackBegin.x;
        int32_t y = _currentTrackBegin.y;
        int32_t z = _currentTrackBegin.z;
        int32_t direction = direction_reverse(_currentTrackPieceDirection);
        CoordsXYE next_track;
        if (track_block_get_next_from_zero(x, y, z, ride, direction, &next_track, &z, &direction, false))
        {
            _rideConstructionState = RIDE_CONSTRUCTION_STATE_SELECTED;
            _currentTrackBegin.x = next_track.x;
            _currentTrackBegin.y = next_track.y;
            _currentTrackBegin.z = z;
            _currentTrackPieceDirection = next_track.element->GetDirection();
            _currentTrackPieceType = next_track.element->AsTrack()->GetTrackType();
            _currentTrackSelectionFlags = 0;
            _rideConstructionArrowPulseTime = 0;
            return true;
        }
    }
    return false;
}

/**
 *
 *  rct2: 0x006B58EF
 */
bool ride_are_all_possible_entrances_and_exits_built(Ride* ride)
{
    if (ride_type_has_flag(ride->type, RIDE_TYPE_FLAG_IS_SHOP))
        return true;

    for (int32_t i = 0; i < MAX_STATIONS; i++)
    {
        if (ride->stations[i].Start.isNull())
        {
            continue;
        }
        if (ride_get_entrance_location(ride, i).isNull())
        {
            gGameCommandErrorText = STR_ENTRANCE_NOT_YET_BUILT;
            return false;
        }
        if (ride_get_exit_location(ride, i).isNull())
        {
            gGameCommandErrorText = STR_EXIT_NOT_YET_BUILT;
            return false;
        }
    }
    return true;
}

/**
 *
 *  rct2: 0x006B59C6
 */
void invalidate_test_results(Ride* ride)
{
    ride->measurement = {};
    ride->excitement = RIDE_RATING_UNDEFINED;
    ride->lifecycle_flags &= ~RIDE_LIFECYCLE_TESTED;
    ride->lifecycle_flags &= ~RIDE_LIFECYCLE_TEST_IN_PROGRESS;
    if (ride->lifecycle_flags & RIDE_LIFECYCLE_ON_TRACK)
    {
        for (int32_t i = 0; i < ride->num_vehicles; i++)
        {
            uint16_t spriteIndex = ride->vehicles[i];
            if (spriteIndex != SPRITE_INDEX_NULL)
            {
                rct_vehicle* vehicle = GET_VEHICLE(spriteIndex);
                vehicle->update_flags &= ~VEHICLE_UPDATE_FLAG_TESTING;
            }
        }
    }
    window_invalidate_by_number(WC_RIDE, ride->id);
}

/**
 *
 *  rct2: 0x006B7481
 *
 * @param rideIndex (dl)
 * @param reliabilityIncreaseFactor (ax)
 */
void ride_fix_breakdown(Ride* ride, int32_t reliabilityIncreaseFactor)
{
    ride->lifecycle_flags &= ~RIDE_LIFECYCLE_BREAKDOWN_PENDING;
    ride->lifecycle_flags &= ~RIDE_LIFECYCLE_BROKEN_DOWN;
    ride->lifecycle_flags &= ~RIDE_LIFECYCLE_DUE_INSPECTION;
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_MAIN | RIDE_INVALIDATE_RIDE_LIST | RIDE_INVALIDATE_RIDE_MAINTENANCE;

    if (ride->lifecycle_flags & RIDE_LIFECYCLE_ON_TRACK)
    {
        for (int32_t i = 0; i < ride->num_vehicles; i++)
        {
            uint16_t spriteIndex = ride->vehicles[i];
            while (spriteIndex != SPRITE_INDEX_NULL)
            {
                rct_vehicle* vehicle = GET_VEHICLE(spriteIndex);
                vehicle->update_flags &= ~VEHICLE_UPDATE_FLAG_ZERO_VELOCITY;
                vehicle->update_flags &= ~VEHICLE_UPDATE_FLAG_BROKEN_CAR;
                vehicle->update_flags &= ~VEHICLE_UPDATE_FLAG_BROKEN_TRAIN;
                spriteIndex = vehicle->next_vehicle_on_train;
            }
        }
    }

    uint8_t unreliability = 100 - ride->reliability_percentage;
    ride->reliability += reliabilityIncreaseFactor * (unreliability / 2);
}

/**
 *
 *  rct2: 0x006DE102
 */
void ride_update_vehicle_colours(Ride* ride)
{
    if (ride->type == RIDE_TYPE_SPACE_RINGS || ride_type_has_flag(ride->type, RIDE_TYPE_FLAG_16))
    {
        gfx_invalidate_screen();
    }

    for (int32_t i = 0; i <= MAX_VEHICLES_PER_RIDE; i++)
    {
        int32_t carIndex = 0;
        uint16_t spriteIndex = ride->vehicles[i];
        VehicleColour colours = {};

        while (spriteIndex != SPRITE_INDEX_NULL)
        {
            rct_vehicle* vehicle = GET_VEHICLE(spriteIndex);
            switch (ride->colour_scheme_type & 3)
            {
                case RIDE_COLOUR_SCHEME_ALL_SAME:
                    colours = ride->vehicle_colours[0];
                    colours.Ternary = ride->vehicle_colours[0].Ternary;
                    break;
                case RIDE_COLOUR_SCHEME_DIFFERENT_PER_TRAIN:
                    colours = ride->vehicle_colours[i];
                    colours.Ternary = ride->vehicle_colours[i].Ternary;
                    break;
                case RIDE_COLOUR_SCHEME_DIFFERENT_PER_CAR:
                    colours = ride->vehicle_colours[std::min(carIndex, MAX_CARS_PER_TRAIN - 1)];
                    colours.Ternary = ride->vehicle_colours[std::min(carIndex, MAX_CARS_PER_TRAIN - 1)].Ternary;
                    break;
            }

            vehicle->colours.body_colour = colours.Body;
            vehicle->colours.trim_colour = colours.Trim;
            vehicle->colours_extended = colours.Ternary;
            invalidate_sprite_2((rct_sprite*)vehicle);
            spriteIndex = vehicle->next_vehicle_on_train;
            carIndex++;
        }
    }
}

/**
 *
 *  rct2: 0x006DE4CD
 * trainLayout: Originally fixed to 0x00F64E38. This no longer postfixes with 255.
 */
void ride_entry_get_train_layout(int32_t rideEntryIndex, int32_t numCarsPerTrain, uint8_t* trainLayout)
{
    for (int32_t i = 0; i < numCarsPerTrain; i++)
    {
        trainLayout[i] = ride_entry_get_vehicle_at_position(rideEntryIndex, numCarsPerTrain, i);
    }
}

uint8_t ride_entry_get_vehicle_at_position(int32_t rideEntryIndex, int32_t numCarsPerTrain, int32_t position)
{
    rct_ride_entry* rideEntry = get_ride_entry(rideEntryIndex);
    if (position == 0 && rideEntry->front_vehicle != 255)
    {
        return rideEntry->front_vehicle;
    }
    else if (position == 1 && rideEntry->second_vehicle != 255)
    {
        return rideEntry->second_vehicle;
    }
    else if (position == 2 && rideEntry->third_vehicle != 255)
    {
        return rideEntry->third_vehicle;
    }
    else if (position == numCarsPerTrain - 1 && rideEntry->rear_vehicle != 255)
    {
        return rideEntry->rear_vehicle;
    }
    else
    {
        return rideEntry->default_vehicle;
    }
}

// Finds track pieces that a given ride entry has sprites for
uint64_t ride_entry_get_supported_track_pieces(const rct_ride_entry* rideEntry)
{
    static constexpr uint16_t trackPieceRequiredSprites[55] = {
        0x0001u, 0x0001u, 0x0001u, 0x0000u, 0x0006u, 0x0002u, 0x0020u, 0x000E,  0x0003u, 0x0006u, 0x0007u,
        0x0002u, 0x0004u, 0x0001u, 0x0001u, 0x0001u, 0x0001u, 0x0061u, 0x000E,  0x1081u, 0x0001u, 0x0020u,
        0x0020u, 0x0001u, 0x0001u, 0x0000u, 0x0001u, 0x0001u, 0x000C,  0x0061u, 0x0002u, 0x000E,  0x0480u,
        0x0001u, 0x0061u, 0x0001u, 0x0001u, 0x000Fu, 0x0001u, 0x0200u, 0x0007u, 0x0008u, 0x0000u, 0x0000u,
        0x4000u, 0x0008u, 0x0001u, 0x0001u, 0x0061u, 0x0061u, 0x0008u, 0x0008u, 0x0001u, 0x000Eu, 0x000Eu,
    };

    // Only check default vehicle; it's assumed the others will have correct sprites if this one does (I've yet to find an
    // exception, at least)
    auto supportedPieces = std::numeric_limits<uint64_t>::max();
    auto defaultVehicle = rideEntry->GetDefaultVehicle();
    if (defaultVehicle != nullptr)
    {
        const auto defaultSpriteFlags = defaultVehicle->sprite_flags;
        for (size_t i = 0; i < std::size(trackPieceRequiredSprites); i++)
        {
            if ((defaultSpriteFlags & trackPieceRequiredSprites[i]) != trackPieceRequiredSprites[i])
            {
                supportedPieces &= ~(1ULL << i);
            }
        }
    }
    return supportedPieces;
}

static opt::optional<int32_t> ride_get_smallest_station_length(Ride* ride)
{
    opt::optional<int32_t> result;
    for (const auto& station : ride->stations)
    {
        if (!station.Start.isNull())
        {
            if (!result.has_value() || station.Length < *result)
            {
                result = station.Length;
            }
        }
    }
    return result;
}

/**
 *
 *  rct2: 0x006CB3AA
 */
static int32_t ride_get_track_length(Ride* ride)
{
    rct_window* w;
    TileElement* tileElement = nullptr;
    track_circuit_iterator it, slowIt;
    ride_id_t rideIndex;
    int32_t trackType, result;
    CoordsXY trackStart;
    bool foundTrack = false;

    for (int32_t i = 0; i < MAX_STATIONS && !foundTrack; i++)
    {
        const auto& stationTileLoc = ride->stations[i].Start;
        if (stationTileLoc.isNull())
            continue;

        trackStart = stationTileLoc.ToCoordsXY();
        auto z = ride->stations[i].GetBaseZ();

        tileElement = map_get_first_element_at(stationTileLoc.ToCoordsXY());
        if (tileElement == nullptr)
            continue;
        do
        {
            if (tileElement->GetType() != TILE_ELEMENT_TYPE_TRACK)
                continue;

            trackType = tileElement->AsTrack()->GetTrackType();
            if (!(TrackSequenceProperties[trackType][0] & TRACK_SEQUENCE_FLAG_ORIGIN))
                continue;

            if (tileElement->GetBaseZ() != z)
                continue;

            foundTrack = true;
        } while (!foundTrack && !(tileElement++)->IsLastForTile());
    }

    if (foundTrack)
    {
        rideIndex = tileElement->AsTrack()->GetRideIndex();

        w = window_find_by_class(WC_RIDE_CONSTRUCTION);
        if (w != nullptr && _rideConstructionState != RIDE_CONSTRUCTION_STATE_0 && _currentRideIndex == rideIndex)
        {
            ride_construction_invalidate_current_track();
        }

        bool moveSlowIt = true;
        result = 0;
        track_circuit_iterator_begin(&it, { trackStart.x, trackStart.y, tileElement });
        slowIt = it;
        while (track_circuit_iterator_next(&it))
        {
            trackType = it.current.element->AsTrack()->GetTrackType();
            result += TrackPieceLengths[trackType];

            moveSlowIt = !moveSlowIt;
            if (moveSlowIt)
            {
                track_circuit_iterator_next(&slowIt);
                if (track_circuit_iterators_match(&it, &slowIt))
                {
                    return 0;
                }
            }
        }
        return result;
    }
    else
    {
        return 0;
    }
}

/**
 *
 *  rct2: 0x006DD57D
 */
void Ride::UpdateMaxVehicles()
{
    if (subtype == RIDE_ENTRY_INDEX_NULL)
        return;

    rct_ride_entry* rideEntry = get_ride_entry(subtype);
    if (rideEntry == nullptr)
    {
        return;
    }
    rct_ride_entry_vehicle* vehicleEntry;
    uint8_t numCarsPerTrain, numVehicles;
    int32_t maxNumTrains;

    if (rideEntry->cars_per_flat_ride == 0xFF)
    {
        int32_t trainLength;
        num_cars_per_train = std::max(rideEntry->min_cars_in_train, num_cars_per_train);
        min_max_cars_per_train = rideEntry->max_cars_in_train | (rideEntry->min_cars_in_train << 4);

        // Calculate maximum train length based on smallest station length
        auto stationNumTiles = ride_get_smallest_station_length(this);
        if (!stationNumTiles.has_value())
            return;

        auto stationLength = (*stationNumTiles * 0x44180) - 0x16B2A;
        int32_t maxMass = RideData5[type].max_mass << 8;
        int32_t maxCarsPerTrain = 1;
        for (int32_t numCars = rideEntry->max_cars_in_train; numCars > 0; numCars--)
        {
            trainLength = 0;
            int32_t totalMass = 0;
            for (int32_t i = 0; i < numCars; i++)
            {
                vehicleEntry = &rideEntry->vehicles[ride_entry_get_vehicle_at_position(subtype, numCars, i)];
                trainLength += vehicleEntry->spacing;
                totalMass += vehicleEntry->car_mass;
            }

            if (trainLength <= stationLength && totalMass <= maxMass)
            {
                maxCarsPerTrain = numCars;
                break;
            }
        }
        int32_t newCarsPerTrain = std::max(proposed_num_cars_per_train, rideEntry->min_cars_in_train);
        maxCarsPerTrain = std::max(maxCarsPerTrain, (int32_t)rideEntry->min_cars_in_train);
        if (!gCheatsDisableTrainLengthLimit)
        {
            newCarsPerTrain = std::min(maxCarsPerTrain, newCarsPerTrain);
        }
        min_max_cars_per_train = maxCarsPerTrain | (rideEntry->min_cars_in_train << 4);

        switch (mode)
        {
            case RIDE_MODE_CONTINUOUS_CIRCUIT_BLOCK_SECTIONED:
            case RIDE_MODE_POWERED_LAUNCH_BLOCK_SECTIONED:
                maxNumTrains = std::clamp(num_stations + num_block_brakes - 1, 1, 31);
                break;
            case RIDE_MODE_REVERSE_INCLINE_LAUNCHED_SHUTTLE:
            case RIDE_MODE_POWERED_LAUNCH_PASSTROUGH:
            case RIDE_MODE_SHUTTLE:
            case RIDE_MODE_LIM_POWERED_LAUNCH:
            case RIDE_MODE_POWERED_LAUNCH:
                maxNumTrains = 1;
                break;
            default:
                // Calculate maximum number of trains
                trainLength = 0;
                for (int32_t i = 0; i < newCarsPerTrain; i++)
                {
                    vehicleEntry = &rideEntry->vehicles[ride_entry_get_vehicle_at_position(subtype, newCarsPerTrain, i)];
                    trainLength += vehicleEntry->spacing;
                }

                int32_t totalLength = trainLength / 2;
                if (newCarsPerTrain != 1)
                    totalLength /= 2;

                maxNumTrains = 0;
                do
                {
                    maxNumTrains++;
                    totalLength += trainLength;
                } while (totalLength <= stationLength);

                if ((mode != RIDE_MODE_STATION_TO_STATION && mode != RIDE_MODE_CONTINUOUS_CIRCUIT)
                    || !(RideData4[type].flags & RIDE_TYPE_FLAG4_ALLOW_MORE_VEHICLES_THAN_STATION_FITS))
                {
                    maxNumTrains = std::min(maxNumTrains, 31);
                }
                else
                {
                    vehicleEntry = &rideEntry->vehicles[ride_entry_get_vehicle_at_position(subtype, newCarsPerTrain, 0)];
                    int32_t poweredMaxSpeed = vehicleEntry->powered_max_speed;

                    int32_t totalSpacing = 0;
                    for (int32_t i = 0; i < newCarsPerTrain; i++)
                    {
                        vehicleEntry = &rideEntry->vehicles[ride_entry_get_vehicle_at_position(subtype, newCarsPerTrain, i)];
                        totalSpacing += vehicleEntry->spacing;
                    }

                    totalSpacing >>= 13;
                    int32_t trackLength = ride_get_track_length(this) / 4;
                    if (poweredMaxSpeed > 10)
                        trackLength = (trackLength * 3) / 4;
                    if (poweredMaxSpeed > 25)
                        trackLength = (trackLength * 3) / 4;
                    if (poweredMaxSpeed > 40)
                        trackLength = (trackLength * 3) / 4;

                    maxNumTrains = 0;
                    int32_t length = 0;
                    do
                    {
                        maxNumTrains++;
                        length += totalSpacing;
                    } while (maxNumTrains < 31 && length < trackLength);
                }
                break;
        }
        max_trains = maxNumTrains;

        numCarsPerTrain = std::min(proposed_num_cars_per_train, (uint8_t)newCarsPerTrain);
    }
    else
    {
        max_trains = rideEntry->cars_per_flat_ride;
        min_max_cars_per_train = rideEntry->max_cars_in_train | (rideEntry->min_cars_in_train << 4);
        numCarsPerTrain = rideEntry->max_cars_in_train;
        maxNumTrains = rideEntry->cars_per_flat_ride;
    }

    if (gCheatsDisableTrainLengthLimit)
    {
        maxNumTrains = 31;
    }
    numVehicles = std::min(proposed_num_vehicles, (uint8_t)maxNumTrains);

    // Refresh new current num vehicles / num cars per vehicle
    if (numVehicles != num_vehicles || numCarsPerTrain != num_cars_per_train)
    {
        num_cars_per_train = numCarsPerTrain;
        num_vehicles = numVehicles;
        window_invalidate_by_number(WC_RIDE, id);
    }
}

void Ride::UpdateNumberOfCircuits()
{
    if (!CanHaveMultipleCircuits())
    {
        num_circuits = 1;
    }
}

void Ride::SetRideEntry(int32_t rideEntry)
{
    auto colour = ride_get_unused_preset_vehicle_colour(rideEntry);
    auto rideSetVehicleAction = RideSetVehicleAction(id, RideSetVehicleType::RideEntry, rideEntry, colour);
    GameActions::Execute(&rideSetVehicleAction);
}

void Ride::SetNumVehicles(int32_t numVehicles)
{
    auto rideSetVehicleAction = RideSetVehicleAction(id, RideSetVehicleType::NumTrains, numVehicles);
    GameActions::Execute(&rideSetVehicleAction);
}

void Ride::SetNumCarsPerVehicle(int32_t numCarsPerVehicle)
{
    auto rideSetVehicleAction = RideSetVehicleAction(id, RideSetVehicleType::NumCarsPerTrain, numCarsPerVehicle);
    GameActions::Execute(&rideSetVehicleAction);
}

/**
 *
 *  rct2: 0x006CB945
 */
void sub_6CB945(Ride* ride)
{
    if (ride->type != RIDE_TYPE_MAZE)
    {
        for (uint8_t stationId = 0; stationId < MAX_STATIONS; ++stationId)
        {
            if (ride->stations[stationId].Start.isNull())
                continue;

            CoordsXYZ location = { ride->stations[stationId].Start.x * 32, ride->stations[stationId].Start.y * 32,
                                   ride->stations[stationId].GetBaseZ() };
            auto tileHeight = TileCoordsXYZ(location).z;
            uint8_t direction = 0xFF;

            bool specialTrack = false;
            TileElement* tileElement = nullptr;

            while (true)
            {
                if (direction != 0xFF)
                {
                    location.x -= CoordsDirectionDelta[direction].x;
                    location.y -= CoordsDirectionDelta[direction].y;
                }
                tileElement = map_get_first_element_at(location);
                if (tileElement == nullptr)
                    break;

                bool trackFound = false;
                do
                {
                    if (tileElement->base_height != tileHeight)
                        continue;
                    if (tileElement->GetType() != TILE_ELEMENT_TYPE_TRACK)
                        continue;
                    if (tileElement->AsTrack()->GetRideIndex() != ride->id)
                        continue;
                    if (tileElement->AsTrack()->GetSequenceIndex() != 0)
                        continue;
                    if (!(TrackSequenceProperties[tileElement->AsTrack()->GetTrackType()][0] & TRACK_SEQUENCE_FLAG_ORIGIN))
                        continue;

                    trackFound = true;
                    break;
                } while (!(tileElement++)->IsLastForTile());

                if (!trackFound)
                {
                    break;
                }

                tileElement->AsTrack()->SetStationIndex(stationId);
                direction = tileElement->GetDirection();

                if (ride_type_has_flag(ride->type, RIDE_TYPE_FLAG_3))
                {
                    specialTrack = true;
                    break;
                }
            }

            if (!specialTrack)
            {
                continue;
            }

            const rct_preview_track* trackBlock = get_track_def_from_ride(ride, tileElement->AsTrack()->GetTrackType());
            while ((++trackBlock)->index != 0xFF)
            {
                CoordsXYZ blockLocation = location + CoordsXYZ{ CoordsXY{ trackBlock->x, trackBlock->y }.Rotate(direction), 0 };

                bool trackFound = false;
                tileElement = map_get_first_element_at(blockLocation);
                if (tileElement == nullptr)
                    break;
                do
                {
                    if (blockLocation.z != tileElement->GetBaseZ())
                        continue;
                    if (tileElement->GetType() != TILE_ELEMENT_TYPE_TRACK)
                        continue;
                    if (!(TrackSequenceProperties[tileElement->AsTrack()->GetTrackType()][0] & TRACK_SEQUENCE_FLAG_ORIGIN))
                        continue;

                    trackFound = true;
                    break;
                } while (!(tileElement++)->IsLastForTile());

                if (!trackFound)
                {
                    break;
                }

                tileElement->AsTrack()->SetStationIndex(stationId);
            }
        }
    }

    std::vector<TileCoordsXYZD> locations;
    for (uint8_t stationId = 0; stationId < MAX_STATIONS; ++stationId)
    {
        auto entrance = ride_get_entrance_location(ride, stationId);
        if (!entrance.isNull())
        {
            locations.push_back(entrance);
            ride_clear_entrance_location(ride, stationId);
        }

        auto exit = ride_get_exit_location(ride, stationId);
        if (!exit.isNull())
        {
            locations.push_back(exit);
            ride_clear_exit_location(ride, stationId);
        }
    }

    auto locationListIter = locations.cbegin();
    for (const TileCoordsXYZD& locationCoords : locations)
    {
        auto locationList = ++locationListIter;

        bool duplicateLocation = false;
        while (locationList != locations.cend())
        {
            const TileCoordsXYZD& locationCoords2 = *locationList++;
            if (locationCoords.x == locationCoords2.x && locationCoords.y == locationCoords2.y)
            {
                duplicateLocation = true;
                break;
            }
        }

        if (duplicateLocation)
        {
            continue;
        }

        CoordsXY location = { locationCoords.x * 32, locationCoords.y * 32 };

        TileElement* tileElement = map_get_first_element_at(location);
        if (tileElement == nullptr)
            continue;
        do
        {
            if (tileElement->GetType() != TILE_ELEMENT_TYPE_ENTRANCE)
                continue;
            if (tileElement->AsEntrance()->GetRideIndex() != ride->id)
                continue;
            if (tileElement->AsEntrance()->GetEntranceType() > ENTRANCE_TYPE_RIDE_EXIT)
                continue;

            CoordsXY nextLocation = location;
            nextLocation.x += CoordsDirectionDelta[tileElement->GetDirection()].x;
            nextLocation.y += CoordsDirectionDelta[tileElement->GetDirection()].y;

            bool shouldRemove = true;
            TileElement* trackElement = map_get_first_element_at(nextLocation);
            if (trackElement == nullptr)
                continue;
            do
            {
                if (trackElement->GetType() != TILE_ELEMENT_TYPE_TRACK)
                    continue;
                if (trackElement->AsTrack()->GetRideIndex() != ride->id)
                    continue;
                if (trackElement->base_height != tileElement->base_height)
                    continue;

                auto trackType = trackElement->AsTrack()->GetTrackType();
                uint8_t trackSequence = trackElement->AsTrack()->GetSequenceIndex();

                Direction direction = (tileElement->GetDirection() - direction_reverse(trackElement->GetDirection())) & 3;

                if (!(TrackSequenceProperties[trackType][trackSequence] & (1 << direction)))
                {
                    continue;
                }

                uint8_t stationId = 0;
                if (trackType != TRACK_ELEM_MAZE)
                {
                    stationId = trackElement->AsTrack()->GetStationIndex();
                }

                if (tileElement->AsEntrance()->GetEntranceType() == ENTRANCE_TYPE_RIDE_EXIT)
                {
                    if (!ride_get_exit_location(ride, stationId).isNull())
                        break;

                    CoordsXYZD loc = { location, ride->stations[stationId].GetBaseZ(), tileElement->GetDirection() };
                    ride_set_exit_location(ride, stationId, TileCoordsXYZD{ loc });
                }
                else
                {
                    if (!ride_get_entrance_location(ride, stationId).isNull())
                        break;

                    CoordsXYZD loc = { location, ride->stations[stationId].GetBaseZ(), tileElement->GetDirection() };
                    ride_set_entrance_location(ride, stationId, TileCoordsXYZD{ loc });
                }

                tileElement->AsEntrance()->SetStationIndex(stationId);
                shouldRemove = false;
            } while (!(trackElement++)->IsLastForTile());

            if (shouldRemove)
            {
                footpath_queue_chain_reset();
                maze_entrance_hedge_replacement(location.x, location.y, tileElement);
                footpath_remove_edges_at(location, tileElement);
                footpath_update_queue_chains();
                map_invalidate_tile_full(location);
                tile_element_remove(tileElement);
                tileElement--;
            }
        } while (!(tileElement++)->IsLastForTile());
    }
}

void Ride::SetToDefaultInspectionInterval()
{
    uint8_t defaultInspectionInterval = gConfigGeneral.default_inspection_interval;
    if (inspection_interval != defaultInspectionInterval)
    {
        if (defaultInspectionInterval <= RIDE_INSPECTION_NEVER)
        {
            set_operating_setting(id, RideSetSetting::InspectionInterval, defaultInspectionInterval);
        }
    }
}

/**
 *
 *  rct2: 0x006B752C
 */
void Ride::Crash(uint8_t vehicleIndex)
{
    rct_vehicle* vehicle = GET_VEHICLE(vehicles[vehicleIndex]);

    if (!(gScreenFlags & SCREEN_FLAGS_TITLE_DEMO))
    {
        // Open ride window for crashed vehicle
        auto intent = Intent(WD_VEHICLE);
        intent.putExtra(INTENT_EXTRA_VEHICLE, vehicle);
        rct_window* w = context_open_intent(&intent);

        rct_viewport* viewport = window_get_viewport(w);
        if (w != nullptr && viewport != nullptr)
        {
            viewport->flags |= VIEWPORT_FLAG_SOUND_ON;
        }
    }

    FormatNameTo(gCommonFormatArgs);
    if (gConfigNotifications.ride_crashed)
    {
        news_item_add_to_queue(NEWS_ITEM_RIDE, STR_RIDE_HAS_CRASHED, id);
    }
}

void ride_reset_all_names()
{
    for (auto& ride : GetRideManager())
    {
        ride.SetNameToDefault();
    }
}

const uint8_t* ride_seek_available_modes(Ride* ride)
{
    const uint8_t* availableModes;

    if (!gCheatsShowAllOperatingModes)
    {
        availableModes = RideAvailableModes;

        for (int32_t i = 0; i < ride->type; i++)
        {
            while (*(availableModes++) != 255)
            {
            }
        }
    }
    else
    {
        availableModes = AllRideModesAvailable;
    }

    return availableModes;
}

// Gets the approximate value of customers per hour for this ride. Multiplies ride_customers_in_last_5_minutes() by 12.
uint32_t ride_customers_per_hour(const Ride* ride)
{
    return ride_customers_in_last_5_minutes(ride) * 12;
}

// Calculates the number of customers for this ride in the last 5 minutes (or more correctly 9600 game ticks)
uint32_t ride_customers_in_last_5_minutes(const Ride* ride)
{
    uint32_t sum = 0;

    for (int32_t i = 0; i < CUSTOMER_HISTORY_SIZE; i++)
    {
        sum += ride->num_customers[i];
    }

    return sum;
}

rct_vehicle* ride_get_broken_vehicle(Ride* ride)
{
    uint16_t vehicleIndex = ride->vehicles[ride->broken_vehicle];

    if (vehicleIndex == SPRITE_INDEX_NULL)
    {
        return nullptr;
    }

    rct_vehicle* vehicle = GET_VEHICLE(vehicleIndex);
    for (uint8_t i = 0; i < ride->broken_car; i++)
    {
        vehicle = GET_VEHICLE(vehicle->next_vehicle_on_train);
    }

    return vehicle;
}

/**
 *
 *  rct2: 0x006D235B
 */
void Ride::Delete()
{
    custom_name = {};
    measurement = {};
    type = RIDE_TYPE_NULL;
}

void Ride::Renew()
{
    // Set build date to current date (so the ride is brand new)
    build_date = gDateMonthsElapsed;
    reliability = RIDE_INITIAL_RELIABILITY;
}

RideClassification Ride::GetClassification() const
{
    switch (type)
    {
        case RIDE_TYPE_FOOD_STALL:
        case RIDE_TYPE_1D:
        case RIDE_TYPE_DRINK_STALL:
        case RIDE_TYPE_1F:
        case RIDE_TYPE_SHOP:
        case RIDE_TYPE_22:
        case RIDE_TYPE_50:
        case RIDE_TYPE_52:
        case RIDE_TYPE_53:
        case RIDE_TYPE_54:
            return RideClassification::ShopOrStall;
        case RIDE_TYPE_INFORMATION_KIOSK:
        case RIDE_TYPE_TOILETS:
        case RIDE_TYPE_CASH_MACHINE:
        case RIDE_TYPE_FIRST_AID:
            return RideClassification::KioskOrFacility;
        default:
            return RideClassification::Ride;
    }
}

bool Ride::IsRide() const
{
    return GetClassification() == RideClassification::Ride;
}

money16 ride_get_price(const Ride* ride)
{
    if (gParkFlags & PARK_FLAGS_NO_MONEY)
        return 0;
    if (ride->IsRide())
    {
        if (!park_ride_prices_unlocked())
        {
            return 0;
        }
    }
    return ride->price;
}

/**
 * Return the tile_element of an adjacent station at x,y,z(+-2).
 * Returns nullptr if no suitable tile_element is found.
 */
TileElement* get_station_platform(int32_t x, int32_t y, int32_t z, int32_t z_tolerance)
{
    bool foundTileElement = false;
    TileElement* tileElement = map_get_first_element_at({ x, y });
    if (tileElement != nullptr)
    {
        do
        {
            if (tileElement->GetType() != TILE_ELEMENT_TYPE_TRACK)
                continue;
            /* Check if tileElement is a station platform. */
            if (!track_element_is_station(tileElement))
                continue;

            if (z - z_tolerance > tileElement->base_height || z + z_tolerance < tileElement->base_height)
            {
                /* The base height if tileElement is not within
                 * the z tolerance. */
                continue;
            }

            foundTileElement = true;
            break;
        } while (!(tileElement++)->IsLastForTile());
    }
    if (!foundTileElement)
    {
        return nullptr;
    }

    return tileElement;
}

/**
 * Check for an adjacent station to x,y,z in direction.
 */
static bool check_for_adjacent_station(int32_t x, int32_t y, int32_t z, uint8_t direction)
{
    bool found = false;
    int32_t adjX = x;
    int32_t adjY = y;
    for (uint32_t i = 0; i <= RIDE_ADJACENCY_CHECK_DISTANCE; i++)
    {
        adjX += CoordsDirectionDelta[direction].x;
        adjY += CoordsDirectionDelta[direction].y;
        TileElement* stationElement = get_station_platform(adjX, adjY, z, 2);
        if (stationElement != nullptr)
        {
            auto rideIndex = stationElement->AsTrack()->GetRideIndex();
            auto ride = get_ride(rideIndex);
            if (ride != nullptr && (ride->depart_flags & RIDE_DEPART_SYNCHRONISE_WITH_ADJACENT_STATIONS))
            {
                found = true;
            }
        }
    }
    return found;
}

/**
 * Return whether ride has at least one adjacent station to it.
 */
bool ride_has_adjacent_station(Ride* ride)
{
    bool found = false;

    /* Loop through all of the ride stations, checking for an
     * adjacent station on either side. */
    for (int32_t stationNum = 0; stationNum < MAX_STATIONS; stationNum++)
    {
        auto stationStart = ride->stations[stationNum].GetStart();
        if (!stationStart.isNull())
        {
            /* Get the map element for the station start. */
            uint8_t stationZ = ride->stations[stationNum].Height;

            TileElement* stationElement = get_station_platform(stationStart.x, stationStart.y, stationZ, 0);
            if (stationElement == nullptr)
            {
                continue;
            }
            /* Check the first side of the station */
            int32_t direction = stationElement->GetDirectionWithOffset(1);
            found = check_for_adjacent_station(stationStart.x, stationStart.y, stationZ, direction);
            if (found)
                break;
            /* Check the other side of the station */
            direction = direction_reverse(direction);
            found = check_for_adjacent_station(stationStart.x, stationStart.y, stationZ, direction);
            if (found)
                break;
        }
    }
    return found;
}

bool ride_has_station_shelter(Ride* ride)
{
    auto stationObj = ride_get_station_object(ride);
    if (network_get_mode() != NETWORK_MODE_NONE)
    {
        // The server might run in headless mode so no images will be loaded, only check for stations.
        return stationObj != nullptr;
    }
    return stationObj != nullptr && stationObj->BaseImageId != 0;
}

bool ride_has_ratings(const Ride* ride)
{
    return ride->excitement != RIDE_RATING_UNDEFINED;
}

const char* ride_type_get_enum_name(int32_t rideType)
{
    static constexpr const char* RideTypeEnumNames[RIDE_TYPE_COUNT] = {
        nameof(RIDE_TYPE_SPIRAL_ROLLER_COASTER),
        nameof(RIDE_TYPE_STAND_UP_ROLLER_COASTER),
        nameof(RIDE_TYPE_SUSPENDED_SWINGING_COASTER),
        nameof(RIDE_TYPE_INVERTED_ROLLER_COASTER),
        nameof(RIDE_TYPE_JUNIOR_ROLLER_COASTER),
        nameof(RIDE_TYPE_MINIATURE_RAILWAY),
        nameof(RIDE_TYPE_MONORAIL),
        nameof(RIDE_TYPE_MINI_SUSPENDED_COASTER),
        nameof(RIDE_TYPE_BOAT_HIRE),
        nameof(RIDE_TYPE_WOODEN_WILD_MOUSE),
        nameof(RIDE_TYPE_STEEPLECHASE),
        nameof(RIDE_TYPE_CAR_RIDE),
        nameof(RIDE_TYPE_LAUNCHED_FREEFALL),
        nameof(RIDE_TYPE_BOBSLEIGH_COASTER),
        nameof(RIDE_TYPE_OBSERVATION_TOWER),
        nameof(RIDE_TYPE_LOOPING_ROLLER_COASTER),
        nameof(RIDE_TYPE_DINGHY_SLIDE),
        nameof(RIDE_TYPE_MINE_TRAIN_COASTER),
        nameof(RIDE_TYPE_CHAIRLIFT),
        nameof(RIDE_TYPE_CORKSCREW_ROLLER_COASTER),
        nameof(RIDE_TYPE_MAZE),
        nameof(RIDE_TYPE_SPIRAL_SLIDE),
        nameof(RIDE_TYPE_GO_KARTS),
        nameof(RIDE_TYPE_LOG_FLUME),
        nameof(RIDE_TYPE_RIVER_RAPIDS),
        nameof(RIDE_TYPE_DODGEMS),
        nameof(RIDE_TYPE_SWINGING_SHIP),
        nameof(RIDE_TYPE_SWINGING_INVERTER_SHIP),
        nameof(RIDE_TYPE_FOOD_STALL),
        nameof(RIDE_TYPE_1D),
        nameof(RIDE_TYPE_DRINK_STALL),
        nameof(RIDE_TYPE_1F),
        nameof(RIDE_TYPE_SHOP),
        nameof(RIDE_TYPE_MERRY_GO_ROUND),
        nameof(RIDE_TYPE_22),
        nameof(RIDE_TYPE_INFORMATION_KIOSK),
        nameof(RIDE_TYPE_TOILETS),
        nameof(RIDE_TYPE_FERRIS_WHEEL),
        nameof(RIDE_TYPE_MOTION_SIMULATOR),
        nameof(RIDE_TYPE_3D_CINEMA),
        nameof(RIDE_TYPE_TOP_SPIN),
        nameof(RIDE_TYPE_SPACE_RINGS),
        nameof(RIDE_TYPE_REVERSE_FREEFALL_COASTER),
        nameof(RIDE_TYPE_LIFT),
        nameof(RIDE_TYPE_VERTICAL_DROP_ROLLER_COASTER),
        nameof(RIDE_TYPE_CASH_MACHINE),
        nameof(RIDE_TYPE_TWIST),
        nameof(RIDE_TYPE_HAUNTED_HOUSE),
        nameof(RIDE_TYPE_FIRST_AID),
        nameof(RIDE_TYPE_CIRCUS),
        nameof(RIDE_TYPE_GHOST_TRAIN),
        nameof(RIDE_TYPE_TWISTER_ROLLER_COASTER),
        nameof(RIDE_TYPE_WOODEN_ROLLER_COASTER),
        nameof(RIDE_TYPE_SIDE_FRICTION_ROLLER_COASTER),
        nameof(RIDE_TYPE_STEEL_WILD_MOUSE),
        nameof(RIDE_TYPE_MULTI_DIMENSION_ROLLER_COASTER),
        nameof(RIDE_TYPE_MULTI_DIMENSION_ROLLER_COASTER_ALT),
        nameof(RIDE_TYPE_FLYING_ROLLER_COASTER),
        nameof(RIDE_TYPE_FLYING_ROLLER_COASTER_ALT),
        nameof(RIDE_TYPE_VIRGINIA_REEL),
        nameof(RIDE_TYPE_SPLASH_BOATS),
        nameof(RIDE_TYPE_MINI_HELICOPTERS),
        nameof(RIDE_TYPE_LAY_DOWN_ROLLER_COASTER),
        nameof(RIDE_TYPE_SUSPENDED_MONORAIL),
        nameof(RIDE_TYPE_LAY_DOWN_ROLLER_COASTER_ALT),
        nameof(RIDE_TYPE_REVERSER_ROLLER_COASTER),
        nameof(RIDE_TYPE_HEARTLINE_TWISTER_COASTER),
        nameof(RIDE_TYPE_MINI_GOLF),
        nameof(RIDE_TYPE_GIGA_COASTER),
        nameof(RIDE_TYPE_ROTO_DROP),
        nameof(RIDE_TYPE_FLYING_SAUCERS),
        nameof(RIDE_TYPE_CROOKED_HOUSE),
        nameof(RIDE_TYPE_MONORAIL_CYCLES),
        nameof(RIDE_TYPE_COMPACT_INVERTED_COASTER),
        nameof(RIDE_TYPE_WATER_COASTER),
        nameof(RIDE_TYPE_AIR_POWERED_VERTICAL_COASTER),
        nameof(RIDE_TYPE_INVERTED_HAIRPIN_COASTER),
        nameof(RIDE_TYPE_MAGIC_CARPET),
        nameof(RIDE_TYPE_SUBMARINE_RIDE),
        nameof(RIDE_TYPE_RIVER_RAFTS),
        nameof(RIDE_TYPE_50),
        nameof(RIDE_TYPE_ENTERPRISE),
        nameof(RIDE_TYPE_52),
        nameof(RIDE_TYPE_53),
        nameof(RIDE_TYPE_54),
        nameof(RIDE_TYPE_55),
        nameof(RIDE_TYPE_INVERTED_IMPULSE_COASTER),
        nameof(RIDE_TYPE_MINI_ROLLER_COASTER),
        nameof(RIDE_TYPE_MINE_RIDE),
        nameof(RIDE_TYPE_59),
        nameof(RIDE_TYPE_LIM_LAUNCHED_ROLLER_COASTER),
    };

    return RideTypeEnumNames[rideType];
}

/**
 *  Searches for a non-null ride type in a ride entry.
 *  If none is found, it will still return RIDE_TYPE_NULL.
 */
uint8_t ride_entry_get_first_non_null_ride_type(const rct_ride_entry* rideEntry)
{
    for (uint8_t i = 0; i < MAX_RIDE_TYPES_PER_RIDE_ENTRY; i++)
    {
        if (rideEntry->ride_type[i] != RIDE_TYPE_NULL)
        {
            return rideEntry->ride_type[i];
        }
    }
    return RIDE_TYPE_NULL;
}

bool ride_type_supports_boosters(uint8_t rideType)
{
    return rideType == RIDE_TYPE_LOOPING_ROLLER_COASTER || rideType == RIDE_TYPE_CORKSCREW_ROLLER_COASTER
        || rideType == RIDE_TYPE_TWISTER_ROLLER_COASTER || rideType == RIDE_TYPE_VERTICAL_DROP_ROLLER_COASTER
        || rideType == RIDE_TYPE_GIGA_COASTER || rideType == RIDE_TYPE_JUNIOR_ROLLER_COASTER;
}

int32_t get_booster_speed(uint8_t rideType, int32_t rawSpeed)
{
    int8_t shiftFactor = RideProperties[rideType].booster_speed_factor;
    if (shiftFactor == 0)
    {
        return rawSpeed;
    }
    else if (shiftFactor > 0)
    {
        return (rawSpeed << shiftFactor);
    }
    else
    {
        // Workaround for an issue with older compilers (GCC 6, Clang 4) which would fail the build
        int8_t shiftFactorAbs = std::abs(shiftFactor);
        return (rawSpeed >> shiftFactorAbs);
    }
}

void fix_invalid_vehicle_sprite_sizes()
{
    for (const auto& ride : GetRideManager())
    {
        for (uint16_t j = 0; j <= MAX_VEHICLES_PER_RIDE; j++)
        {
            uint16_t rideSpriteIndex = ride.vehicles[j];
            while (rideSpriteIndex != SPRITE_INDEX_NULL)
            {
                rct_vehicle* vehicle = try_get_vehicle(rideSpriteIndex);
                if (vehicle == nullptr)
                {
                    break;
                }

                rct_ride_entry_vehicle* vehicleEntry = vehicle_get_vehicle_entry(vehicle);
                if (vehicleEntry == nullptr)
                {
                    break;
                }

                if (vehicle->sprite_width == 0)
                {
                    vehicle->sprite_width = vehicleEntry->sprite_width;
                }
                if (vehicle->sprite_height_negative == 0)
                {
                    vehicle->sprite_height_negative = vehicleEntry->sprite_height_negative;
                }
                if (vehicle->sprite_height_positive == 0)
                {
                    vehicle->sprite_height_positive = vehicleEntry->sprite_height_positive;
                }
                rideSpriteIndex = vehicle->next_vehicle_on_train;
            }
        }
    }
}

bool ride_entry_has_category(const rct_ride_entry* rideEntry, uint8_t category)
{
    for (int32_t i = 0; i < MAX_CATEGORIES_PER_RIDE; i++)
    {
        if (rideEntry->category[i] == category)
        {
            return true;
        }
    }

    return false;
}

int32_t ride_get_entry_index(int32_t rideType, int32_t rideSubType)
{
    int32_t subType = rideSubType;

    if (subType == RIDE_ENTRY_INDEX_NULL)
    {
        uint8_t* availableRideEntries = get_ride_entry_indices_for_ride_type(rideType);
        for (uint8_t* rideEntryIndex = availableRideEntries; *rideEntryIndex != RIDE_ENTRY_INDEX_NULL; rideEntryIndex++)
        {
            rct_ride_entry* rideEntry = get_ride_entry(*rideEntryIndex);
            if (rideEntry == nullptr)
            {
                return RIDE_ENTRY_INDEX_NULL;
            }

            // Can happen in select-by-track-type mode
            if (!ride_entry_is_invented(*rideEntryIndex) && !gCheatsIgnoreResearchStatus)
            {
                continue;
            }

            if (!RideGroupManager::RideTypeIsIndependent(rideType))
            {
                subType = *rideEntryIndex;
                break;
            }
        }
        if (subType == RIDE_ENTRY_INDEX_NULL)
        {
            subType = availableRideEntries[0];
        }
    }

    return subType;
}

StationObject* ride_get_station_object(const Ride* ride)
{
    auto& objManager = GetContext()->GetObjectManager();
    return static_cast<StationObject*>(objManager.GetLoadedObject(OBJECT_TYPE_STATION, ride->entrance_style));
}

// Normally, a station has at most one entrance and one exit, which are at the same height
// as the station. But in hacked parks, neither can be taken for granted. This code ensures
// that the ride->entrances and ride->exits arrays will point to one of them. There is
// an ever-so-slight chance two entrances/exits for the same station reside on the same tile.
// In cases like this, the one at station height will be considered the "true" one.
// If none exists at that height, newer and higher placed ones take precedence.
void determine_ride_entrance_and_exit_locations()
{
    log_verbose("Inspecting ride entrance / exit locations");

    for (auto& ride : GetRideManager())
    {
        for (int32_t stationIndex = 0; stationIndex < MAX_STATIONS; stationIndex++)
        {
            TileCoordsXYZD entranceLoc = ride.stations[stationIndex].Entrance;
            TileCoordsXYZD exitLoc = ride.stations[stationIndex].Exit;
            bool fixEntrance = false;
            bool fixExit = false;

            // Skip if the station has no entrance
            if (!entranceLoc.isNull())
            {
                const EntranceElement* entranceElement = map_get_ride_entrance_element_at(entranceLoc.ToCoordsXYZD(), false);

                if (entranceElement == nullptr || entranceElement->GetRideIndex() != ride.id
                    || entranceElement->GetStationIndex() != stationIndex)
                {
                    fixEntrance = true;
                }
                else
                {
                    ride.stations[stationIndex].Entrance.direction = (uint8_t)entranceElement->GetDirection();
                }
            }

            if (!exitLoc.isNull())
            {
                const EntranceElement* entranceElement = map_get_ride_exit_element_at(exitLoc.ToCoordsXYZD(), false);

                if (entranceElement == nullptr || entranceElement->GetRideIndex() != ride.id
                    || entranceElement->GetStationIndex() != stationIndex)
                {
                    fixExit = true;
                }
                else
                {
                    ride.stations[stationIndex].Exit.direction = (uint8_t)entranceElement->GetDirection();
                }
            }

            if (!fixEntrance && !fixExit)
            {
                continue;
            }

            // At this point, we know we have a disconnected entrance or exit.
            // Search the map to find it. Skip the outer ring of invisible tiles.
            bool alreadyFoundEntrance = false;
            bool alreadyFoundExit = false;
            for (uint8_t x = 1; x < MAXIMUM_MAP_SIZE_TECHNICAL - 1; x++)
            {
                for (uint8_t y = 1; y < MAXIMUM_MAP_SIZE_TECHNICAL - 1; y++)
                {
                    TileElement* tileElement = map_get_first_element_at(TileCoordsXY{ x, y }.ToCoordsXY());

                    if (tileElement != nullptr)
                    {
                        do
                        {
                            if (tileElement->GetType() != TILE_ELEMENT_TYPE_ENTRANCE)
                            {
                                continue;
                            }
                            const EntranceElement* entranceElement = tileElement->AsEntrance();
                            if (entranceElement->GetRideIndex() != ride.id)
                            {
                                continue;
                            }
                            if (entranceElement->GetStationIndex() != stationIndex)
                            {
                                continue;
                            }

                            // The expected height is where entrances and exit reside in non-hacked parks.
                            const uint8_t expectedHeight = ride.stations[stationIndex].Height;

                            if (fixEntrance && entranceElement->GetEntranceType() == ENTRANCE_TYPE_RIDE_ENTRANCE)
                            {
                                if (alreadyFoundEntrance)
                                {
                                    if (ride.stations[stationIndex].Entrance.z == expectedHeight)
                                        continue;
                                    if (ride.stations[stationIndex].Entrance.z > entranceElement->base_height)
                                        continue;
                                }

                                // Found our entrance
                                TileCoordsXYZD newEntranceLoc = {
                                    x,
                                    y,
                                    entranceElement->base_height,
                                    (uint8_t)entranceElement->GetDirection(),
                                };
                                ride_set_entrance_location(&ride, stationIndex, newEntranceLoc);
                                alreadyFoundEntrance = true;

                                log_verbose(
                                    "Fixed disconnected entrance of ride %d, station %d to x = %d, y = %d and z = %d.", ride.id,
                                    stationIndex, x, y, entranceElement->base_height);
                            }
                            else if (fixExit && entranceElement->GetEntranceType() == ENTRANCE_TYPE_RIDE_EXIT)
                            {
                                if (alreadyFoundExit)
                                {
                                    if (ride.stations[stationIndex].Exit.z == expectedHeight)
                                        continue;
                                    if (ride.stations[stationIndex].Exit.z > entranceElement->base_height)
                                        continue;
                                }

                                // Found our exit
                                ride_set_exit_location(
                                    &ride, stationIndex,
                                    { x, y, entranceElement->base_height, (uint8_t)entranceElement->GetDirection() });
                                alreadyFoundExit = true;

                                log_verbose(
                                    "Fixed disconnected exit of ride %d, station %d to x = %d, y = %d and z = %d.", ride.id,
                                    stationIndex, x, y, entranceElement->base_height);
                            }
                        } while (!(tileElement++)->IsLastForTile());
                    }
                }
            }

            if (fixEntrance && !alreadyFoundEntrance)
            {
                ride_clear_entrance_location(&ride, stationIndex);
                log_verbose("Cleared disconnected entrance of ride %d, station %d.", ride.id, stationIndex);
            }
            if (fixExit && !alreadyFoundExit)
            {
                ride_clear_exit_location(&ride, stationIndex);
                log_verbose("Cleared disconnected exit of ride %d, station %d.", ride.id, stationIndex);
            }
        }
    }
}

void ride_clear_leftover_entrances(Ride* ride)
{
    tile_element_iterator it;

    tile_element_iterator_begin(&it);
    while (tile_element_iterator_next(&it))
    {
        if (it.element->GetType() == TILE_ELEMENT_TYPE_ENTRANCE
            && it.element->AsEntrance()->GetEntranceType() != ENTRANCE_TYPE_PARK_ENTRANCE
            && it.element->AsEntrance()->GetRideIndex() == ride->id)
        {
            tile_element_remove(it.element);
            tile_element_iterator_restart_for_tile(&it);
        }
    }
}

std::string Ride::GetName() const
{
    uint8_t args[32]{};
    FormatNameTo(args);
    return format_string(STR_STRINGID, args);
}

size_t Ride::FormatNameTo(void* argsV) const
{
    auto args = (uint8_t*)argsV;
    if (!custom_name.empty())
    {
        auto str = custom_name.c_str();
        set_format_arg_on(args, 0, rct_string_id, STR_STRING);
        set_format_arg_on(args, 2, void*, str);
        return sizeof(rct_string_id) + sizeof(void*);
    }
    else
    {
        auto rideTypeName = RideNaming[type].name;
        if (RideGroupManager::RideTypeIsIndependent(type))
        {
            auto rideEntry = GetRideEntry();
            if (rideEntry != nullptr)
            {
                rideTypeName = rideEntry->naming.name;
            }
        }
        else if (RideGroupManager::RideTypeHasRideGroups(type))
        {
            auto rideEntry = GetRideEntry();
            if (rideEntry != nullptr)
            {
                auto rideGroup = RideGroupManager::GetRideGroup(type, rideEntry);
                if (rideGroup != nullptr)
                {
                    rideTypeName = rideGroup->Naming.name;
                }
            }
        }
        set_format_arg_on(args, 0, rct_string_id, 1);
        set_format_arg_on(args, 2, rct_string_id, rideTypeName);
        set_format_arg_on(args, 4, uint16_t, default_name_number);
        return sizeof(rct_string_id) + sizeof(rct_string_id) + sizeof(uint16_t);
    }
}
