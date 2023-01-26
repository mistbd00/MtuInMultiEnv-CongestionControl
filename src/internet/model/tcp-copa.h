#ifndef TCPCOPA_H
#define TCPCOPA_H

#include "ns3/traced-value.h"
#include "ns3/tcp-congestion-ops.h"
#include "ns3/tcp-socket-base.h"
// #include "windowed-filter.h"
#include "ns3/windowed-filter.h"

namespace ns3
{
	/**
	 * \brief The Copa implementation through TCP congestion control mechanism
	 *
	 * TODO
	 * refered to the mechanism described in NSID18 article Copa: Practical
	 * Delay-Based Congestion Control for the Internet
	 *
	 */

	class TcpCopa : public TcpCongestionOps
	{
	public:
		static TypeId GetTypeId(void);

		TcpCopa();

		TcpCopa(const TcpCopa &sock);

		~TcpCopa();

		std::string GetName() const;

		virtual uint32_t GetSsThresh(Ptr<const TcpSocketState> tcb,
									 uint32_t bytesInFlight);

		virtual void IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked);

		virtual void PktsAcked(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked,
							   const Time &rtt);

		virtual void CongestionStateSet(Ptr<TcpSocketState> tcb,
										const TcpSocketState::TcpCongState_t newState);

		virtual void CwndEvent(Ptr<TcpSocketState> tcb,
							   const TcpSocketState::TcpCAEvent_t event);

		virtual Ptr<TcpCongestionOps> Fork();

	private:
		typedef enum
		{
			None,
			Up,	  // cwnd is increasing
			Down, // cwnd is decreasing
		} Direction;
		typedef struct
		{
			uint64_t velocity{1};
			Direction direction{None};
			// number of rtts direction has remained same
			int64_t numTimesDirectionSame{0};
			// updated every srtt
			uint64_t lastRecordedCwnd{0};
			Time lastRecordTime{Time(0)};
		} VelocityState;

		typedef enum
		{
			StartMode,
			DefaultMode,
			CompetingMode,
		} Mode;
		typedef struct
		{
			Mode mode{StartMode};
			bool isCompeting{false};
			Time cycleStartTime{Time(0)};
			// max rtt sample over the first RTT(srtt)s
			Time rttMax{0};
			// min queue delta over the last RTT(srtt)
			Time queueDelayMin{0};
		} ModeState;

		VelocityState m_velocityState;
		ModeState m_modeState;

		typedef WindowedFilter<Time, MinFilter<Time>, int64_t, int64_t> RTTMinFilter;
		RTTMinFilter m_rttMinFilter;
		RTTMinFilter m_rttStandingFilterBase;
		Time m_rttStandingFilterResetTime{Time(0)};
		Time m_rttMinFilterResetInterval{Time("10s")};

		TracedValue<uint32_t> m_cWnd{0};
		TracedValue<double> m_txRate{0.0};
		double m_targetTxRate{0.0};
		TracedValue<Time> m_rttStanding{Time(0)};
		Time m_rttMin{0};
		Time m_srtt{Time("100ms")};
		double m_srttUpdateFactor{0.9};
		double m_defaultDelta{0.5};
		double m_delta{m_defaultDelta};
		TracedValue<uint32_t> m_queueDelayThresh{0};
		uint32_t m_queueDelay{0};

		double m_deltaThresh{0.05};

		void ModeSwitch(Time newRtt, Time currentTime);
		inline Mode ModeDetector();
		void UpdateRttStanding(Time newRtt, Time currentTime);
		inline void UpdateSrtt(Time newRtt);
		inline void UpdateRttMin(Time newRtt, Time currentTime);
		inline void UpdateQueueDelay();
		inline void UpdateTargetTxRate();
		inline void UpdateCwndAndTxRate(uint32_t currentCWnd);
		inline void AdjustCwnd();
		void AdjustVelocityState(Time currentTime);
		inline void AdjustDelta();
	};

} // namespace ns3

#endif // TCPCOPA_H