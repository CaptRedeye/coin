#include <el/ext.h>

#include "../eng.h"
#include "../script.h"
#include "../coin-model.h"

namespace Coin {

class COIN_CLASS DevCoinEng : public CoinEng {
	typedef CoinEng base;
public:
	DevCoinEng(CoinDb& cdb)
		:	base(cdb)
	{}
protected:
	Int64 GetMinRelayTxFee() override { return ChainParams.MinTxFee; }

	void UpdateMinFeeForTxOuts(Int64& minFee, const Int64& baseFee, const Tx& tx) override {
		minFee += baseFee / 10 * tx.TxOuts().size();
	}

	Target GetNextTargetRequired(const Block& blockLast, const Block& block)  override {
		TimeSpan targetSpan = TimeSpan::FromDays(blockLast.Height < 10700 ? 14 : 1);
		int nInterval = int(targetSpan.TotalSeconds) / int(ChainParams.BlockSpan.TotalSeconds);
		if (blockLast.Height < 10 || blockLast.Height < 10700 && ((blockLast.Height+1) % nInterval != 0))
			return blockLast.DifficultyTarget;
		BigInteger avg;
		vector<DateTime> vTimes;
		Block prev = blockLast;
		for (int i=0; i<nInterval-1; ++i, prev=prev.GetPrevBlock()) {
			avg += BigInteger(prev.get_DifficultyTarget());
			vTimes.push_back(prev.Timestamp);
		}
		TimeSpan spanActual = blockLast.get_Timestamp() - prev.get_Timestamp();
		if (blockLast.Height <= 10800)
			avg = blockLast.get_DifficultyTarget();
		else {
			avg /= (nInterval-1);
			sort(vTimes.begin(), vTimes.end());
			spanActual = (vTimes[vTimes.size()-6]-vTimes[6]) * double((nInterval-1)/(vTimes.size()-12));
		}			
		spanActual = std::min(std::max(spanActual, targetSpan/4), targetSpan*4);
		return Target(avg*spanActual.get_Ticks()/targetSpan.get_Ticks());
	}
};

static class DevCoinChainParams : public ChainParams {
	typedef ChainParams base;
public:
	DevCoinChainParams(bool)
		:	base("DevCoin", false)
	{	
		ChainParams::Add(_self);
	}

	DevCoinEng *CreateEng(CoinDb& cdb) override { return new DevCoinEng(cdb); }
} s_devcoinParams(true);


} // Coin::

