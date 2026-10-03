/*
	Clarity
	Copyright (C) 2026 Joseph Pasfield

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

/**
 * @brief ISPC implementation of NetworkState::activateFeatureSingle's accumulator update.
 *
 * Adds one feature's weight row onto an accumulator half, one program instance per
 * element of the layer, in place of the original scalar for-loop.
 *
 * @param acc        Accumulator half to update in place (layer1Size elements).
 * @param weights    Base of the network's feature-weight table.
 * @param index      Feature index; the weight row starts at (index * layer1Size).
 * @param layer1Size Width of the accumulator / weight row.
 */
export void ActivateFeatureSingle_ispc( uniform int16 acc[], const uniform int16 weights[], uniform int32 index, uniform int32 layer1Size )
{
	const uniform int32 base = index * layer1Size;

	foreach( i = 0 ... layer1Size )
	{
		acc[i] += weights[base + i];
	}
}

/**
 * @brief ISPC implementation of NetworkState::disableFeatureSingle's accumulator update.
 *
 * Subtracts one feature's weight row from an accumulator half, one program instance per
 * element of the layer, in place of the original scalar for-loop.
 *
 * @param acc        Accumulator half to update in place (layer1Size elements).
 * @param weights    Base of the network's feature-weight table.
 * @param index      Feature index; the weight row starts at (index * layer1Size).
 * @param layer1Size Width of the accumulator / weight row.
 */
export void DisableFeatureSingle_ispc( uniform int16 acc[], const uniform int16 weights[], uniform int32 index, uniform int32 layer1Size )
{
	const uniform int32 base = index * layer1Size;

	foreach( i = 0 ... layer1Size )
	{
		acc[i] -= weights[base + i];
	}
}

/**
 * @brief ISPC implementation of NetworkState::activateFeatureAndPush's accumulator update.
 *
 * Writes both accumulator halves of the next stack slot as (previous slot + weight row),
 * one program instance per element of the layer, in place of the original scalar for-loop.
 *
 * @param blackNext  Black half of the next stack slot to write.
 * @param blackPrev  Black half of the current stack slot to read from.
 * @param blackIndex Feature index for black; its weight row starts at (blackIndex * layer1Size).
 * @param whiteNext  White half of the next stack slot to write.
 * @param whitePrev  White half of the current stack slot to read from.
 * @param whiteIndex Feature index for white; its weight row starts at (whiteIndex * layer1Size).
 * @param weights    Base of the network's feature-weight table.
 * @param layer1Size Width of the accumulator / weight row.
 */
export void ActivateFeatureAndPush_ispc( uniform int16 blackNext[], const uniform int16 blackPrev[], uniform int32 blackIndex,
                                         uniform int16 whiteNext[], const uniform int16 whitePrev[], uniform int32 whiteIndex,
                                         const uniform int16 weights[], uniform int32 layer1Size )
{
	const uniform int32 blackBase = blackIndex * layer1Size;
	const uniform int32 whiteBase = whiteIndex * layer1Size;

	foreach( i = 0 ... layer1Size )
	{
		blackNext[i] = blackPrev[i] + weights[blackBase + i];
		whiteNext[i] = whitePrev[i] + weights[whiteBase + i];
	}
}

/**
 * @brief ISPC implementation of NetworkState::forward's SCReLU activation and dot product.
 *
 * Computes sum_i( CReLU(us[i])^2 * usWeights[i] ) + sum_i( CReLU(them[i])^2 * themWeights[i] ),
 * where CReLU(x) = clamp(x, 0, qa), one program instance per element of the layer, accumulating
 * a per-lane partial sum across the whole range before a single cross-lane reduction at the end.
 *
 * @param us              Our accumulator half (layer1Size elements).
 * @param them            Opponent's accumulator half (layer1Size elements).
 * @param weights         Base of the output-weight table.
 * @param bucketIncrement Offset of this bucket's weight row within weights (2 * layer1Size * bucket).
 * @param layer1Size      Width of the accumulator / weight rows.
 * @param qa              Upper clamp bound for the CReLU activation.
 * @return The accumulated 32-bit dot product, before the caller's final scale/bias step.
 */
export uniform int32 Forward_ispc( const uniform int16 us[], const uniform int16 them[], const uniform int16 weights[],
                                   uniform int32 bucketIncrement, uniform int32 layer1Size, uniform int32 qa )
{
	varying int32 sum = 0;

	foreach( i = 0 ... layer1Size )
	{
		const int32 u = clamp( (int32)us[i], 0, qa );
		const int32 uw = weights[i + bucketIncrement];
		sum += u * u * uw;

		const int32 t = clamp( (int32)them[i], 0, qa );
		const int32 tw = weights[layer1Size + i + bucketIncrement];
		sum += t * t * tw;
	}

	return reduce_add( sum );
}
