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

struct NetworkUpdate
{
	NetworkUpdate( int32_t s, int32_t p )
	{
		square = s;
		piece = p;
	}

	NetworkUpdate()
	{
		square = 0;
		piece = 0;
	}

	int32_t square;
	int32_t piece;
};

struct NetworkUpdates
{
	NetworkUpdates()
	{
		numAdds = 0;
		numSubs = 0;
		bucketChange = false;
	}

	void pushAdd( int32_t square, int32_t piece )
	{
		adds[numAdds] = NetworkUpdate( square, piece );
		numAdds++;
	}

	void pushSub( int32_t square, int32_t piece )
	{
		subs[numSubs] = NetworkUpdate( square, piece );
		numSubs++;
	}

	void pushBucket( int32_t square, int32_t color )
	{
		bucketChange = true;
		bucketUpdate = NetworkUpdate( square, color );
	}

	std::array<NetworkUpdate, 2> adds;
	std::array<NetworkUpdate, 2> subs;
	NetworkUpdate bucketUpdate;
	int32_t numAdds;
	int32_t numSubs;
	bool bucketChange;
};
