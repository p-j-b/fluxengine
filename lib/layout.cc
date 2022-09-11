#include "lib/globals.h"
#include "lib/layout.h"
#include "lib/proto.h"
#include "lib/environment.h"
#include <fmt/format.h>

static Local<std::map<std::pair<int, int>, std::unique_ptr<Layout>>>
    layoutCache;

unsigned Layout::remapTrackPhysicalToLogical(unsigned ptrack)
{
    return (ptrack - config.drive().head_bias()) / config.drive().head_width();
}

static unsigned getTrackStep()
{
    unsigned track_step =
        (config.tpi() == 0) ? 1 : (config.drive().tpi() / config.tpi());

    if (track_step == 0)
        Error()
            << "this drive can't write this image, because the head is too big";
    return track_step;
}

unsigned Layout::remapTrackLogicalToPhysical(unsigned ltrack)
{
    return config.drive().head_bias() + ltrack * getTrackStep();
}

std::set<Location> Layout::computeLocations()
{
    std::set<Location> locations;

    std::set<unsigned> tracks;
    if (config.has_tracks())
        tracks = iterate(config.tracks());
    else
        tracks = iterate(0, config.layout().tracks());

    std::set<unsigned> heads;
    if (config.has_heads())
        heads = iterate(config.heads());
    else
        heads = iterate(0, config.layout().sides());

    for (unsigned logicalTrack : tracks)
    {
        for (unsigned head : heads)
            locations.insert(computeLocationFor(logicalTrack, head));
    }

    return locations;
}

Location Layout::computeLocationFor(unsigned logicalTrack, unsigned logicalHead)
{
    if ((logicalTrack < config.layout().tracks()) &&
        (logicalHead < config.layout().sides()))
    {
        unsigned track_step = getTrackStep();
        unsigned physicalTrack =
            config.drive().head_bias() + logicalTrack * track_step;

        return {.physicalTrack = physicalTrack,
            .logicalTrack = logicalTrack,
            .head = logicalHead,
            .groupSize = track_step};
    }

    Error() << fmt::format(
        "track {}.{} is not part of the image", logicalTrack, logicalHead);
}

std::vector<std::pair<int, int>> Layout::getTrackOrdering(
    unsigned guessedTracks, unsigned guessedSides)
{
    auto layout = config.layout();
    int tracks = layout.has_tracks() ? layout.tracks() : guessedTracks;
    int sides = layout.has_sides() ? layout.sides() : guessedSides;

    std::vector<std::pair<int, int>> ordering;
    switch (layout.order())
    {
        case LayoutProto::CHS:
        {
            for (int track = 0; track < tracks; track++)
            {
                for (int side = 0; side < sides; side++)
                    ordering.push_back(std::make_pair(track, side));
            }
            break;
        }

        case LayoutProto::HCS:
        {
            for (int side = 0; side < sides; side++)
            {
                for (int track = 0; track < tracks; track++)
                    ordering.push_back(std::make_pair(track, side));
            }
            break;
        }

        default:
            Error() << "LAYOUT: invalid track ordering";
    }

    return ordering;
}

std::vector<unsigned> Layout::expandSectorList(
    const SectorListProto& sectorsProto)
{
    std::vector<unsigned> sectors;

    if (sectorsProto.has_count())
    {
        if (sectorsProto.sector_size() != 0)
            Error() << "LAYOUT: if you use a sector count, you can't use an "
                       "explicit sector list";

        int startSector = sectorsProto.start_sector();
        for (int i = 0; i < sectorsProto.count(); i++)
            sectors.push_back(startSector + i);
    }
    else if (sectorsProto.sector_size() > 0)
    {
        for (int sectorId : sectorsProto.sector())
            sectors.push_back(sectorId);
    }
    else
        Error() << "LAYOUT: no sectors in track!";

    return sectors;
}

const Layout& Layout::getLayoutOfTrack(unsigned track, unsigned side)
{
    auto& layout = (*layoutCache)[std::make_pair(track, side)];
    if (!layout)
    {
        layout.reset(new Layout());

        LayoutProto::LayoutdataProto layoutdata;
        for (const auto& f : config.layout().layoutdata())
        {
            if (f.has_track() && f.has_up_to_track() &&
                ((track < f.track()) || (track > f.up_to_track())))
                continue;
            if (f.has_track() && !f.has_up_to_track() && (track != f.track()))
                continue;
            if (f.has_side() && (f.side() != side))
                continue;

            layoutdata.MergeFrom(f);
        }

        layout->numTracks = config.layout().tracks();
        layout->numSides = config.layout().sides();
        layout->sectorSize = layoutdata.sector_size();
        layout->diskSectorOrder = expandSectorList(layoutdata.physical());
        layout->logicalSectorOrder = layout->diskSectorOrder;
        std::sort(
            layout->diskSectorOrder.begin(), layout->diskSectorOrder.end());
        layout->numSectors = layout->logicalSectorOrder.size();

        if (layoutdata.has_filesystem())
        {
            layout->filesystemSectorOrder =
                expandSectorList(layoutdata.filesystem());
            if (layout->filesystemSectorOrder.size() != layout->numSectors)
                Error()
                    << "filesystem sector order list doesn't contain the right "
                       "number of sectors";
        }
        else
        {
            for (unsigned sectorId : layout->logicalSectorOrder)
                layout->filesystemSectorOrder.push_back(sectorId);
        }

        for (int i = 0; i < layout->numSectors; i++)
        {
            unsigned l = layout->logicalSectorOrder[i];
            unsigned f = layout->filesystemSectorOrder[i];
            layout->filesystemToLogicalSectorMap[f] = l;
            layout->logicalToFilesystemSectorMap[l] = f;
        }
    }

    return *layout;
}
