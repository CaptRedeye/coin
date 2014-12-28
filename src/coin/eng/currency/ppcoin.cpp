/*######     Copyright (c) 1997-2015 Ufasoft  http://ufasoft.com  mailto:support@ufasoft.com,  Sergey Pavlov  mailto:dev@ufasoft.com #########################################################################################################
#                                                                                                                                                                                                                                            #
# This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation;  either version 3, or (at your option) any later version.          #
# This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.   #
# You should have received a copy of the GNU General Public License along with this program; If not, see <http://www.gnu.org/licenses/>                                                                                                      #
############################################################################################################################################################################################################################################*/

#include <el/ext.h>

#include "../proof-of-stake.h"


namespace Coin {

const Version VER_PPCOIN_STAKE_MODIFIER(0, 50),
			VER_PPCOIN_V04_STAKE_MODIFIER(0, 93);

static DateTime DtV03Switch(2013, 3, 20, 17, 20),
			DtV04Switch(2014, 5, 5, 14, 26, 40);


class PPCoinBlockObj : public PosBlockObj {
	typedef PPCoinBlockObj class_type;
	typedef PosBlockObj base;
public:

	PPCoinBlockObj() {
	}

	bool IsV02Protocol(const DateTime& dt) const {
		return dt < DtV03Switch;
	}

	bool IsV04Protocol() const override {
		return Timestamp >= DtV04Switch;
	}

	void CheckCoinStakeTimestamp() override {
		DateTime dtTx = GetTxObj(1).Timestamp;
		if (!IsV02Protocol(dtTx))
			base::CheckCoinStakeTimestamp();
		else if (dtTx > Timestamp || Timestamp > dtTx+TimeSpan::FromSeconds(MAX_FUTURE_SECONDS)) 
			Throw(E_COIN_TimestampViolation);
	}

	void WriteKernelStakeModifier(BinaryWriter& wr, const Block& blockPrev) const override {
		DateTime dtTx = GetTxObj(1).Timestamp;
		if (!IsV02Protocol(dtTx))
			base::WriteKernelStakeModifier(wr, blockPrev);
		else
			wr << DifficultyTargetBits;		
	}
protected:
	bool StakeEntropyBit() const override {
		return IsV04Protocol() ? base::StakeEntropyBit()
			:	Hash160(Signature)[19] & 0x80;
	}
};


class COIN_CLASS PPCoinEng : public PosEng {
	typedef PosEng base;
public:
	PPCoinEng(CoinDb& cdb)
		:	base(cdb)
	{
	}

protected:
	void TryUpgradeDb() override {
		if (Db->CheckUserVersion() < VER_PPCOIN_V04_STAKE_MODIFIER)
			Db->Recreate();
		base::TryUpgradeDb();
	}

	BlockObj *CreateBlockObj() override { return new PPCoinBlockObj; }
};

static class PPCoinChainParams : public ChainParams {
	typedef ChainParams base;
public:
	PPCoinChainParams(bool)
		:	base("PPCoin", false)
	{	
//!!!R		MaxPossibleTarget = Target(0x1D00FFFF);
		ChainParams::Add(_self);
	}

	PPCoinEng *CreateEng(CoinDb& cdb) override { return new PPCoinEng(cdb); }
} s_ppcoinParams(true);




} // Coin::

