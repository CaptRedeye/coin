#include <el/ext.h>

#include "proof-of-stake.h"

namespace Coin {

static const TimeSpan STAKE_TARGET_SPACING = TimeSpan::FromMinutes(10);
static const TimeSpan MODIFIER_INTERVAL = TimeSpan::FromHours(6);
const int MODIFIER_INTERVAL_RATIO = 3;

static TimeSpan GetStakeModifierSelectionIntervalSection(int nSection) {
	return TimeSpan::FromSeconds(double(((Int64)MODIFIER_INTERVAL.TotalSeconds * 63 / (63 + ((63 - nSection) * (MODIFIER_INTERVAL_RATIO - 1))))));
}

static TimeSpan GetStakeModifierSelectionInterval() {
    TimeSpan r = 0;
    for (int nSection=0; nSection<64; nSection++)
        r += GetStakeModifierSelectionIntervalSection(nSection);
    return r;
}

static const TimeSpan STAKE_MODIFIER_SELECTION_INTERVAL = GetStakeModifierSelectionInterval();

Int64 PosTxObj::GetCoinAge() const {
	if (m_coinAge == numeric_limits<UInt64>::max()) {
		CoinEng& eng = Eng();

		BigInteger centSecond = 0;
		if (Tx((PosTxObj*)this).IsCoinBase())
			m_coinAge = 0;
		else {
			EXT_FOR (const TxIn& txIn, TxIns()) {
				Tx txPrev = Tx::FromDb(txIn.PrevOutPoint.TxHash);
				DateTime dtPrev = ((PosTxObj*)txPrev.m_pimpl.get())->Timestamp;
				if (Timestamp < dtPrev)
					Throw(E_COIN_TimestampViolation);
				if (eng.GetBlockByHeight(txPrev.Height).get_Timestamp()+TimeSpan::FromDays(30) <= Timestamp)
					centSecond += BigInteger(txPrev.TxOuts()[txIn.PrevOutPoint.Index].Value) * int((Timestamp-dtPrev).TotalSeconds) / (eng.ChainParams.CoinValue/100);
			}
			m_coinAge = explicit_cast<Int64>(centSecond / (100 * 24*60*60));
		}
	}
	return m_coinAge;
}

Int64 GetProofOfStakeReward(Int64 coinAge) {
	CoinEng& eng = Eng();
	return eng.ChainParams.CoinValue * eng.ChainParams.AnnualPercentageRate * coinAge * 33 / (100*(365*33 + 8));
}

void PosTxObj::CheckCoinStakeReward(Int64 reward, const Target& target) const {
	PosEng& eng = (PosEng&)Eng();
	Tx tx((TxObj*)this);
	Int64 posReward = eng.GetProofOfStakeReward(GetCoinAge(), target, Timestamp);
	if (reward > posReward - tx.GetMinFee(1, eng.AllowFreeTxes) + eng.ChainParams.MinTxFee)
		Throw(E_COIN_StakeRewardExceeded);
}

HashValue PosBlockObj::Hash() const {
	if (!m_hash) {
		MemoryStream ms;
		BinaryWriter(ms).Ref() << Ver << PrevBlockHash << MerkleRoot() << (UInt32)to_time_t(Timestamp) << get_DifficultyTarget() << Nonce;
		ConstBuf mb = ms;
		switch (Eng().ChainParams.HashAlgo) {
		case HashAlgo::Sha256:
			m_hash = Coin::Hash(mb);
			break;
		case HashAlgo::SCrypt:
			m_hash = ScryptHash(mb);
			break;
		default:
			Throw(E_NOTIMPL);
		}
	}
	return m_hash.get();
}

void PosBlockObj::Write(DbWriter& wr) const {
	base::Write(wr);
	CoinSerialized::WriteBlob(wr, Signature);
	byte flags = (ProofType()==ProofOf::Stake ? 1 : 0) | (!!StakeModifier ? 4 : 0);			
	wr << flags;
	if (ProofType() == ProofOf::Stake)
		wr << HashProofOfStake();
	if (!!StakeModifier)
		wr << StakeModifier.get();
}

void PosBlockObj::Read(const DbReader& rd) {
	base::Read(rd);
	Signature = CoinSerialized::ReadBlob(rd);
	byte flags = rd.ReadByte();
	if (flags & 1) {
		HashValue h;
		rd >> h;
		m_hashProofOfStake = h;
	}
//		m_stakeEntropyBit = flags & 2;
	if (flags & 4)
		StakeModifier = rd.ReadUInt64();
}

void PosBlockObj::WriteKernelStakeModifier(BinaryWriter& wr, const Block& blockPrev) const {
	CoinEng& eng = Eng();
	Block b = blockPrev;
	DateTime dt = b.Timestamp;
	for (DateTime dtTarget=dt+STAKE_MODIFIER_SELECTION_INTERVAL; dt<dtTarget;) {
		if (b == eng.BestBlock())
			Throw(E_COIN_CoinstakeCheckTargetFailed);
		b = eng.GetBlockByHeight(b.Height+1);
		if (!!PosBlockObj::Of(b).StakeModifier)
			dt = b.Timestamp;
	}
//	TRC(2, hex << PosBlockObj::Of(b).StakeModifier.get() << " at height " << dec << b.Height << " timestamp " << dt);
	wr << PosBlockObj::Of(b).StakeModifier.get();
}

HashValue PosBlockObj::HashProofOfStake() const {
	if (!m_hashProofOfStake) {
		CoinEng& eng = Eng();
		DBG_LOCAL_IGNORE_NAME(E_COIN_TxNotFound, ignE_COIN_TxNotFound);

		const Tx& tx = get_Txes()[1];
		const TxIn& txIn = tx.TxIns()[0];
		Tx txPrev = Tx::FromDb(txIn.PrevOutPoint.TxHash);
		DateTime dtTx = ((PosTxObj*)tx.m_pimpl.get())->Timestamp,
			   dtPrev = ((PosTxObj*)txPrev.m_pimpl.get())->Timestamp;
		if (dtTx < dtPrev)
			Throw(E_COIN_TimestampViolation);
		VerifySignature(txPrev, tx, 0);

		Block blockPrev = eng.GetBlockByHeight(txPrev.Height);
		if (blockPrev.get_Timestamp()+TimeSpan::FromDays(30) > dtTx)
			Throw(E_COIN_CoinsAreTooRecent);
	
		Int64 val = txPrev.TxOuts()[txIn.PrevOutPoint.Index].Value;
		BigInteger cdays = BigInteger(val) * Int64(std::min(dtTx-dtPrev, TimeSpan::FromDays(90)).TotalSeconds) / (24 * 60 * 60 * eng.ChainParams.CoinValue);

		MemoryStream ms;
		BinaryWriter wr(ms);
		WriteKernelStakeModifier(wr, blockPrev);
		
		wr << (UInt32)to_time_t(blockPrev.get_Timestamp());
		MemoryStream msBlock;
		BinaryWriter wrPrev(msBlock);
		blockPrev.WriteHeader(wrPrev);
		CoinSerialized::WriteVarInt(wrPrev, blockPrev.get_Txes().size());
		EXT_FOR (const Tx& t, blockPrev.get_Txes()) {
			if (Coin::Hash(t) == txIn.PrevOutPoint.TxHash) {
				wr << UInt32(ConstBuf(msBlock).Size);
				break;
			}
			wrPrev << t;
		}

		wr << (UInt32)to_time_t(dtPrev) << UInt32(txIn.PrevOutPoint.Index) << (UInt32)to_time_t(dtTx);
		HashValue h = Coin::Hash(ms);

		byte ar[33];
		memcpy(ar, h.data(), 32);
		ar[32] = 0;
		if (BigInteger(ar, sizeof ar) > BigInteger(get_DifficultyTarget())*cdays) {
			DBG_LOCAL_IGNORE_NAME(E_COIN_CoinstakeCheckTargetFailed, E_COIN_CoinstakeCheckTargetFailed);
	#ifdef X_DEBUG//!!!D
			TRC(2, hex << BigInteger(ar, sizeof ar));
			TRC(2, hex << BigInteger(get_DifficultyTarget())*cdays);
	#endif
			Throw(E_COIN_CoinstakeCheckTargetFailed);
		}
		m_hashProofOfStake = h;
	}
	return m_hashProofOfStake.get();
}

Target PosEng::GetNextTargetRequired(const Block& blockLast, const Block& block) {
	ProofOf proofType = block.ProofType;
	Block prev = blockLast;
	while (prev && prev.ProofType != proofType)
		prev = prev.PrevBlockHash==HashValue() ? Block(nullptr) : prev.GetPrevBlock();
	if (!prev)
		return ChainParams.InitTarget;
	Block prevPrev = prev.PrevBlockHash==HashValue() ? Block(nullptr) : prev.GetPrevBlock();
	while (prevPrev && prevPrev.ProofType != proofType)
		prevPrev = prevPrev.PrevBlockHash==HashValue() ? Block(nullptr) : prevPrev.GetPrevBlock();
	if (!prevPrev || prevPrev.PrevBlockHash==HashValue())
		return ChainParams.InitTarget;

	TimeSpan nTargetSpacing = proofType==ProofOf::Stake ? STAKE_TARGET_SPACING : std::min(GetTargetSpacingWorkMax(blockLast.Timestamp), TimeSpan::FromMinutes(10)*(blockLast.Height - prev.Height + 1));
	Int64 interval = TimeSpan::FromDays(7).Ticks / nTargetSpacing.Ticks;
	BigInteger r = BigInteger(prev.get_DifficultyTarget()) * (nTargetSpacing.Ticks*(interval-1) + (prev.get_Timestamp() - prevPrev.get_Timestamp()).Ticks*2) / (nTargetSpacing.Ticks*(interval+1));

//		TRC(2, hex << r << "  " << int((prev.Timestamp - prevPrev.Timestamp).TotalSeconds) << " s");

	return std::min(dynamic_cast<PosBlockObj*>(blockLast.m_pimpl.get())->GetTargetLimit(block), Target(r));
}

bool PosBlockObj::VerifySignatureByTxOut(const TxOut& txOut) {
	HashValue160 hash160;
	Blob pk;
	int cbPk = txOut.TryParseDestination(hash160, pk);
	return (33==cbPk || 65==cbPk) && ECDsa(CngKey::Import(pk, CngKeyBlobFormat::OSslEccPublicBlob)).VerifyHash(Hash(), Signature);
}

void PosBlockObj::CheckSignature() {
	CoinEng& eng = Eng();
	const vector<Tx>& txes = get_Txes();

	if (0 == Signature.Size) {
		if (Hash() == eng.ChainParams.Genesis)
			return;
	} else if (VerifySignatureByTxOut(ProofType()==ProofOf::Stake ? txes[1].TxOuts()[1] : txes[0].TxOuts()[0]))
		return;
	Throw(E_COIN_BadBlockSignature);
}

void PosBlockObj::Check(bool bCheckMerkleRoot) {
	CoinEng& eng = Eng();
	const vector<Tx>& txes = get_Txes();

	base::Check(bCheckMerkleRoot);

	for (int i=2; i<txes.size(); ++i)
		if (txes[i].IsCoinStake())
			Throw(E_COIN_CoinstakeInWrongPos);

	if (Timestamp > GetTxObj(0).Timestamp+TimeSpan::FromSeconds(MAX_FUTURE_SECONDS))
		Throw(E_COIN_CoinbaseTimestampIsTooEarly);
	if (ProofType() == ProofOf::Stake) {
		CheckCoinStakeTimestamp();
		CheckProofOfStake();
	}
}

#ifdef X_DEBUG

unordered_map<int, UInt32> g_h2modsum;

UInt32 PosBlockObj::StakeModifierChecksum(UInt64 modifier) {
	MemoryStream ms;
	BinaryWriter wr(ms);
	if (Height != 0) {
		ASSERT(g_h2modsum.count(Height-1));
		wr << g_h2modsum[Height-1];
	}
	UInt32 flags = 0;
	if (!!m_hashProofOfStake)
		flags |= 1;
	if (StakeEntropyBit())
		flags |= 2;
	if (!!StakeModifier)
		flags |= 4;

	wr << flags;
	if (!!m_hashProofOfStake)
		wr << m_hashProofOfStake.get();
	else
		wr << HashValue();
	if (!!StakeModifier)
		wr << StakeModifier.get();
	else
		wr << modifier;
	HashValue h = Coin::Hash(ms);
	UInt32 r =  *(UInt32*)(h.data()+28);
	g_h2modsum[Height] = r;
	TRC(2, Height << "  " << hex << r);
/*!!! Novacoin Specific
	switch (Height) {
	case 0:
		if (r != 0x0e00670b)
			Throw(E_FAIL);
		break;
	case 6000:
		if (r != 0xb7cbc5d3)
			Throw(E_FAIL);
		break;
	case 12661:
		if (r != 0x5d84115d)
			Throw(E_FAIL);
		break;
	}
	*/
	return r;
}


#endif // _DEBUG


void PosBlockObj::ComputeStakeModifier() {
	UInt64 modifier = 0;
	if (0 == Height)
		StakeModifier = 0;
	else {
		PosEng& eng = (PosEng&)Eng();
		Block blockPrev = Block(this).GetPrevBlock();
		DateTime dtModifier;
		for (Block b=blockPrev;; b=b.GetPrevBlock()) {
			PosBlockObj& bo = PosBlockObj::Of(b);
			if (!!bo.StakeModifier) {
				modifier = bo.StakeModifier.get();
				dtModifier = b.Timestamp;
				break;
			}
			if (b.Height == 0)
				Throw(E_FAIL);
		}

		DateTime dtStartOfPrevInterval = DateTime::from_time_t(to_time_t(blockPrev.get_Timestamp()) / (int)MODIFIER_INTERVAL.TotalSeconds * (int)MODIFIER_INTERVAL.TotalSeconds);
		if (dtModifier < dtStartOfPrevInterval) {
			typedef multimap<DateTime, Block> CCandidates;
			CCandidates candidates;
			DateTime dtStart = dtStartOfPrevInterval - STAKE_MODIFIER_SELECTION_INTERVAL,
					dtStop = dtStart;
			for (Block b=blockPrev; b.Timestamp >= dtStart; b=b.GetPrevBlock()) {
				candidates.insert(make_pair(b.Timestamp, b));								//!!! order of same-Timestamp blocks is not defined.
				if (b.Height == 0)
					break;
			}

			UInt64 r = 0;
			unordered_set<int> selectedBlocks;
			for (int i=0, n=std::min(64, (int)candidates.size()); i<n; ++i) {
				dtStop += GetStakeModifierSelectionIntervalSection(i);
				Block b(nullptr);
				HashValue hashBest;
				EXT_FOR (const CCandidates::value_type& cd, candidates) {
					if (b && cd.second.Timestamp > dtStop)
						break;
					if (!selectedBlocks.count(cd.second.Height)) {
						PosBlockObj& bo = PosBlockObj::Of(cd.second);
						MemoryStream ms;
						BinaryWriter(ms).Ref() << bo.HashProof() << modifier;
						HashValue hash = Coin::Hash(ms);
						if (bo.ProofType()==ProofOf::Stake)
							memset((byte*)memmove(hash.data(), hash.data()+4, 28) + 28, 0, 4);
						if (!b || hash < hashBest) {
							hashBest = hash;
							b = cd.second;
						}
					}
				}
				if (!b)
					Throw(E_FAIL);
				r |= UInt64(PosBlockObj::Of(b).StakeEntropyBit()) << i;
				selectedBlocks.insert(b.Height);
			}
			StakeModifier = r;
		}
	}
#ifdef X_DEBUG//!!!D
	StakeModifierChecksum(modifier);
#endif
}

class CheckPointMessage : public CoinMessage {
	typedef CoinMessage base;
public:
	Int32 Ver;
	HashValue Hash;
	Blob Msg, Sig;

	CheckPointMessage()
		:	base("checkpoint")
		,	Ver(1)
	{}

	void Write(BinaryWriter& wr) const override {
		WriteBlob(wr, Msg);
		WriteBlob(wr, Sig);
	}

	void Read(const BinaryReader& rd) override {
		Msg = ReadBlob(rd);
		Sig = ReadBlob(rd);
		CMemReadStream stm(Msg);
		BinaryReader(stm) >> Ver >> Hash;
	}
protected:
	void Process(P2P::Link& link) override;
};

void CheckPointMessage::Process(P2P::Link& link) {
	CoinEng& eng = Eng();
	ECDsa dsa(CngKey::Import(eng.ChainParams.CheckpointMasterPubKey, CngKeyBlobFormat::OSslEccPublicBlob));
	if (!dsa.VerifyHash(Coin::Hash(Msg), Sig))
		Throw(E_COIN_CheckpointVerifySignatureFailed);
	//!!! TODO

}

PosEng::PosEng(CoinDb& cdb)
	:	base(cdb)
{
	MaxBlockVersion = 3;
	AllowFreeTxes = false;
}

Int64 PosEng::GetSubsidy(int height, const HashValue& prevBlockHash, double difficulty, bool bForCheck) {
	Int64 centValue = ChainParams.CoinValue / 100;
	double dr = GetMaxSubsidy() / (pow(difficulty, ChainParams.PowOfDifficultyToHalfSubsidy) * centValue);
	Int64 r = Int64(ceil(dr)) * centValue;
	return r;
}

CoinMessage *PosEng::CreateCheckPointMessage() {
	return new CheckPointMessage;
}

Int64 PosEng::GetProofOfStakeReward(Int64 coinAge, const Target& target, const DateTime& dt) {
	return ChainParams.CoinValue * ChainParams.AnnualPercentageRate * coinAge * 33 / (100*(365*33 + 8));
}

} // Coin::

