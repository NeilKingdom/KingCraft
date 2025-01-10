/**
 * @file chunk_factory.cpp
 * @author Neil Kingdom
 * @since 16-10-2024
 * @version 1.0
 * @brief A singleton class which constructs Chunk objects.
 */

#include "chunk_factory.hpp"

/**
 * @brief Returns the single instance of ChunkFactory.
 * @since 24-10-2024
 * @returns The ChunkFactory instance
 */
ChunkFactory &ChunkFactory::get_instance()
{
    static ChunkFactory instance;
    return instance;
}

/**
 * @brief Creates a Chunk object given a set of input parameters.
 * @since 24-10-2024
 * @param[in] m_chunk_trns A 4x4 matrix which determines the offset of the chunk relative to the world origin
 * @param[in] faces A bitmask representing the faces of the chunk to be rendered
 * @returns The constructed Chunk object
 */
std::shared_ptr<Chunk> ChunkFactory::make_chunk(const vec3 location, const uint8_t faces) const
{
    auto chunk = std::make_shared<Chunk>(Chunk());
    BlockFactory &block_factory = BlockFactory::get_instance();
    GameState &game = GameState::get_instance();
    ssize_t chunk_size = game.chunk_size;
    assert(chunk_size > 1);

    std::memcpy(chunk->location, location, sizeof(vec3));
    chunk->faces = faces;

    struct BlockData
    {
        uint8_t faces;
        BlockType type;
    };

    std::vector<std::vector<std::vector<BlockData>>> tmp_data;
    tmp_data.resize(
        chunk_size,
        std::vector<std::vector<BlockData>>(
            chunk_size,
            std::vector<BlockData>(chunk_size)
        )
    );

    std::vector<std::vector<uint8_t>> heights;
    heights.resize(chunk_size + 2, std::vector<uint8_t>(chunk_size + 2));

    const float scale = 0.05f;

    for (ssize_t y = -1; y < chunk_size + 1; ++y)
    {
        for (ssize_t x = -1; x < chunk_size + 1; ++x)
        {
            heights[y + 1][x + 1] = game.pn.octave_perlin(
                -location[0] * chunk_size + x,
                 location[1] * chunk_size + y,
                 0.8f, 1, scale, 0, 15
            );
        }
    }

    for (ssize_t y = 1; y < chunk_size + 1; ++y)
    {
        for (ssize_t x = 1; x < chunk_size + 1; ++x)
        {
            ssize_t z = heights[y][x];
            tmp_data[z][y - 1][x - 1].faces = 0;

            // Determine block types
            for (ssize_t i = 0; i < chunk_size; ++i)
            {
                // TODO: Add other block types at different z values
                if (i > z)
                {
                    tmp_data[i][y - 1][x - 1].type = BlockType::AIR;
                }
                else
                {
                    tmp_data[i][y - 1][x - 1].type = BlockType::GRASS;
                }
            }

            // Bottom
            tmp_data[0][y - 1][x - 1].faces |= BOTTOM;

            // Top
            if (z == heights[y][x])
            {
                tmp_data[z][y - 1][x - 1].faces |= TOP;
            }

            // Front
            if (z > heights[y][x - 1])
            {
                tmp_data[z][y - 1][x - 1].faces |= FRONT;
            }

            // Back
            if (z > heights[y][x + 1])
            {
                tmp_data[z][y - 1][x - 1].faces |= BACK;
            }

            // Left
            if (z > heights[y - 1][x])
            {
                tmp_data[z][y - 1][x - 1].faces |= LEFT;
            }

            // Right
            if (z > heights[y + 1][x])
            {
                tmp_data[z][y - 1][x - 1].faces |= RIGHT;
            }
        }
    }

    // Create the actual Block objects for the chunk

    chunk->blocks.resize(chunk_size);
    for (ssize_t z = 0; z < chunk_size; ++z)
    {
        chunk->blocks[z].resize(chunk_size);
        for (ssize_t y = 0; y < chunk_size; ++y)
        {
            chunk->blocks[z][y].resize(chunk_size);
            for (ssize_t x = 0; x < chunk_size; ++x)
            {
                chunk->blocks[z][y][x] = block_factory.make_block(
                    tmp_data[z][y][x].type,
                    vec3{
                        -(location[0] * chunk_size) + x,
                         (location[1] * chunk_size) + y,
                         (location[2] * chunk_size) + z
                    },
                    tmp_data[z][y][x].faces
                );
            }
        }
    }

    chunk->flatten_block_data();
    return chunk;
}
