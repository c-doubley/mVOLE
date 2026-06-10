# pragma once
#include "libOTe/Dpf/RegularDpf.h"
#include "coproto/Socket/LocalAsyncSock.h"
#include "libOTe/Dpf/SparseDpf.h"
#include <algorithm>
#include <numeric>
#include "libOTe/Dpf/TernaryDpf.h"
#include "libOTe/TwoChooseOne/SoftSpokenOT/SoftSpokenShOtExt.h"
#include "Dpf.h"


void RegularPrimeDpf_Test()
{

	PRNG prng(sysRandomSeed());
	u64 domain = 1<<19;
	u64 numPoints = 10;
	std::vector<u64> points0(numPoints);
	std::vector<u64> points1(numPoints);
	std::vector<u64> values0(numPoints);
	std::vector<u64> values1(numPoints);
	for (u64 i = 0; i < numPoints; ++i)
	{
		points1[i] = prng.get<u64>();
		points0[i] = (prng.get<u64>() % domain) ^ points1[i];
		values0[i] = prng.get<u64>()%2305843009213693951;
		values1[i] = prng.get<u64>()%2305843009213693951;
	}
    CoeffCtxIntegerPrime_64 ctx;
	std::array<oc::RegularPrimeDpf, 2> dpf;
	dpf[0].init(0, domain, numPoints);
	dpf[1].init(1, domain, numPoints);

	auto baseCount = dpf[0].baseOtCount();

	std::array<std::vector<block>, 2> baseRecv;
	std::array<std::vector<std::array<block, 2>>, 2> baseSend;
	std::array<BitVector, 2> baseChoice;
	baseRecv[0].resize(baseCount);
	baseRecv[1].resize(baseCount);
	baseSend[0].resize(baseCount);
	baseSend[1].resize(baseCount);
	baseChoice[0].resize(baseCount);
	baseChoice[1].resize(baseCount);
	baseChoice[0].randomize(prng);
	baseChoice[1].randomize(prng);
	for (u64 i = 0; i < baseCount; ++i)
	{
		baseSend[0][i] = prng.get();
		baseSend[1][i] = prng.get();
		baseRecv[0][i] = baseSend[1][i][baseChoice[0][i]];
		baseRecv[1][i] = baseSend[0][i][baseChoice[1][i]];
	}
	dpf[0].setBaseOts(baseSend[0], baseRecv[0], baseChoice[0]);
	dpf[1].setBaseOts(baseSend[1], baseRecv[1], baseChoice[1]);

	std::array<Matrix<u64>, 2> output;
	std::array<Matrix<u8>, 2> tags;
	output[0].resize(numPoints, domain);
	output[1].resize(numPoints, domain);
	tags[0].resize(numPoints, domain);
	tags[1].resize(numPoints, domain);

	auto sock = coproto::LocalAsyncSocket::makePair();
	macoro::sync_wait(macoro::when_all_ready(
		dpf[0].expand(points0, values0, prng.get(), [&](auto k, auto i, u64 v, block t) { output[0](k, i) = v; tags[0](k, i) = t.get<u8>(0) & 1; }, sock[0]),
		dpf[1].expand(points1, values1, prng.get(), [&](auto k, auto i, u64 v, block t) { output[1](k, i) = v; tags[1](k, i) = t.get<u8>(0) & 1; }, sock[1])
	));
	for (u64 i = 0; i < domain; ++i)
	{
		for (u64 k = 0; k < numPoints; ++k)
		{
			auto p = points0[k] ^ points1[k];
			u64 act; 
			 ctx.plus(act, output[0][k][i], output[1][k][i]);
			auto t = i == p ? 1 : 0;
			auto tAct = tags[0][k][i] ^ tags[1][k][i];
			u64 exp=0;
			if (t)
				ctx.plus(exp, values0[k], values1[k]);
			if (!ctx.eq(exp, act))
			{	
				throw RTE_LOC;
			}
			if (t != tAct)
				throw RTE_LOC;
		}
	}
}


template<typename F, typename Ctx>
void generate_prime_additive_sharing(const std::vector<F> &poly_coeff0, const std::vector<F> &poly_coeff1, std::vector<F> &value0, std::vector<F> &value1)
{
	PRNG prng(sysRandomSeed());
	Ctx mctx;
	u64 index=0;
	for(int i=0;i<poly_coeff0.size();i++)
	{
			for(int j=0;j<poly_coeff1.size();j++)
		{
			F temp;
			mctx.mul(temp,poly_coeff0[i],poly_coeff1[j]);
			mctx.fromBlock(value0[index], prng.get());
			mctx.minus(value1[index], temp, value0[index]);
			index=index+1;
		}
	}
}


template<typename F, typename Ctx>
float prime_dpf(const std::vector<F> &poly_coeff0, const std::vector<F>&poly_coeff1, const std::vector<F> &poly_points0,const std::vector<F>&poly_points1,
	 std::vector<F>&polyA, std::vector<F> &polyB, u64 depth, u64 length, u64 domain)
{
	PRNG prng(sysRandomSeed());
	u64 numPoints=poly_points0.size();
	u64 num_points=numPoints*numPoints;
	std::vector<F> values0(num_points), values1(num_points);
	std::vector<F> points0(num_points), points1(num_points);
	generate_prime_additive_sharing<F,Ctx>(poly_coeff0,poly_coeff1, values0, values1);
	Ctx ctx;
	
//	std::cout<<"Here2"<<std::endl;

	for (u64 i = 0; i < numPoints; ++i)
	{
		for (u64 j = 0; j < numPoints; ++j)
		{
			points0[i*numPoints+j]=poly_points0[i];
			points1[i*numPoints+j]=poly_points1[j];
		}
	}
//	std::cout<<"Here3"<<std::endl;
	std::array<oc::RegularPrimeDpf, 2> dpf;
	dpf[0].init(0, domain, num_points);
	dpf[1].init(1, domain, num_points);
  //  std::cout<<"Here4"<<std::endl;
	auto baseCount = dpf[0].baseOtCount();
   // std::cout<<baseCount<<std::endl;
	std::array<std::vector<block>, 2> baseRecv;
	std::array<std::vector<std::array<block, 2>>, 2> baseSend;
	std::array<BitVector, 2> baseChoice;
	baseRecv[0].resize(baseCount);
	baseRecv[1].resize(baseCount);
	baseSend[0].resize(baseCount);
	baseSend[1].resize(baseCount);
	baseChoice[0].resize(baseCount);
	baseChoice[1].resize(baseCount);
	baseChoice[0].randomize(prng);
	baseChoice[1].randomize(prng);
	for (u64 i = 0; i < baseCount; ++i)
	{
		baseSend[0][i] = prng.get();
		baseSend[1][i] = prng.get();
		baseRecv[0][i] = baseSend[1][i][baseChoice[0][i]];
		baseRecv[1][i] = baseSend[0][i][baseChoice[1][i]];
	}
	dpf[0].setBaseOts(baseSend[0], baseRecv[0], baseChoice[0]);
	dpf[1].setBaseOts(baseSend[1], baseRecv[1], baseChoice[1]);

	std::array<Matrix<F>, 2> output;
	std::array<Matrix<u8>, 2> tags;
	output[0].resize(num_points, domain);
	output[1].resize(num_points, domain);
	tags[0].resize(num_points, domain);
	tags[1].resize(num_points, domain);

	auto sock = coproto::LocalAsyncSocket::makePair();
	
std::thread t0([&]() 
  {
     macoro::sync_wait(macoro::when_all_ready(
		dpf[0].expand(points0, values0, prng.get(), [&](auto k, auto i, u64 v, block t) { output[0](k, i) = v; tags[0](k, i) = t.get<u8>(0) & 1; }, sock[0])));
		for(int i=0;i<numPoints;i++)
		{
			for(int j=0;j<numPoints;j++)
			{
				 u64 temp=(i^j)*domain;
				for(int k=0;k<domain;k++)
				{
                    ctx.plus(polyA[k+temp],polyA[k+temp], output[0][i*numPoints+j][k]);
				}
			}
		}
			
	});
  std::thread t1([&]() 
  {
      macoro::sync_wait(macoro::when_all_ready(
		dpf[1].expand(points1, values1, prng.get(), [&](auto k, auto i, u64 v, block t) { output[1](k, i) = v; tags[1](k, i) = t.get<u8>(0) & 1; }, sock[1])));
//	std::cout<<"dpf  is over"<<std::endl;
		for(int i=0;i<numPoints;i++)
				{
					for(int j=0;j<numPoints;j++)
					{
						u64 temp=(i^j)*domain;
						for(int k=0;k<domain;k++)
						{
							ctx.plus(polyB[k+temp],polyB[k+temp], output[1][i*numPoints+j][k]);
						}
					}
				}

});
    
    t0.join();
    t1.join();
    
  return (sock[0].bytesSent()+sock[1].bytesSent())/(float)(1024*1024);

	
}


template<typename F, typename Ctx>
void Prime_OLE(u64 depth,u64 block_length, u64 c)
{
	PRNG prng(sysRandomSeed());
	u64 numPoints=ipow(2, block_length);
	u64 domain =ipow(2, depth-block_length);
	u64 length=ipow(2, depth);
	int num_points=numPoints*numPoints;
	std::vector<std::vector<F>> poly_points0(c), poly_points1(c);
	std::vector<std::vector<F>> cross_termA(c*c), cross_termB(c*c);
	std::vector<std::vector<F>>  poly_s0(c), poly_s1(c);
	std::vector<std::vector<F>> s0(c), s1(c), a(c),  a_cross(c*c);
	//std::vector<std::vector<F3x32> poly_points0(numPoints), poly_points1(numPoints),poly_pointe0(numPoints), poly_pointe1(numPoints);
	//std::vector<std::vector<F>> cross_termA(c*c), cross_termB(c*c);
	//std::vector<std::vector<F>>  poly_s0(numPoints), poly_s1(numPoints), poly_e0(numPoints), poly_e1(numPoints);
	//std::vector<std::vector<F>> s0(length),s1(length),e0(length), e1(length), a(length), a_square(length);
	std::vector<F> additive0(length),additive1(length), ole0(length), ole1(length);
	Ctx ctx;
	for(u64 i=0;i<c;i++)
	{
		poly_points0[i].resize(numPoints);
		poly_points1[i].resize(numPoints);
		poly_s0[i].resize(numPoints);
		poly_s1[i].resize(numPoints);
		s0[i].resize(length);
		s1[i].resize(length);
		ctx.zero(s0[i].begin(), s0[i].end());
		ctx.zero(s1[i].begin(), s1[i].end());
		a[i].resize(length);
	}
for(u64 k=0;k<c;k++)
{
	for(u64 i=0;i<length;i++)
	{
		ctx.fromBlock(a[k][i],prng.get());
	}
}
	for(int i=0;i<c*c;i++)
	{
		cross_termA[i].resize(length);
		ctx.zero(cross_termA[i].begin(),cross_termA[i].end());
		cross_termB[i].resize(length);
		ctx.zero(cross_termB[i].begin(),cross_termB[i].end());
        a_cross[i].resize(length);
		for(u64 j=0;j<length;j++)
		{
			ctx.mul(a_cross[i][j], a[i/c][j], a[i%c][j]);
		}		
	}

//	std::cout<<"Here0"<<std::endl;
for(u64 k=0;k<c;k++)
{
	for (u64 i = 0; i < numPoints; ++i)
	{
		poly_points0[k][i] = prng.get<u64>() % domain;
		poly_points1[k][i] = prng.get<u64>() % domain;
		//std::cout << points[i] << " = " << points0[i] <<" + "<< points1[i] << std::endl;
		ctx.fromBlock(poly_s0[k][i], prng.get());
		ctx.fromBlock(poly_s1[k][i], prng.get());
		s0[k][poly_points0[k][i]+i*domain]=poly_s0[k][i];
		s1[k][poly_points1[k][i]+i*domain]=poly_s1[k][i];
		//ctx.minus(points0[i], points[i], points1[i];)
	}
}
	double start=omp_get_wtime();
	float comm_cost=0;
	for(u64 i=0;i<c*c;i++)
	{
		comm_cost+=prime_dpf<F, Ctx>(poly_s0[i/c], poly_s1[i%c], poly_points0[i/c],poly_points1[i%c], cross_termA[i], cross_termB[i], depth, length, domain);
	}
	
	
//	std::cout<<"Here1"<<std::endl;
  std::thread t0([&]() 
  {
	for(u64 i=0;i<c*c;i++)
	{
		prime_wht<F,Ctx>(cross_termA[i], length);
	}
    for(u64 i=0;i<c;i++)
	{
		prime_wht<F,Ctx>(s0[i], length);
	}
			
			for(u64 i=0;i<length;i++)
			{
				F temp,sum;
				ctx.zero(&sum, &sum+1);
				for(u64 j=0;j<c*c;j++)
				{

					ctx.mul(temp,a_cross[j][i], cross_termA[j][i]);
					ctx.plus(sum, temp, sum);
				}
				additive0[i]=sum;
				ctx.zero(&sum, &sum+1);
				for(u64 j=0;j<c;j++)
				{
					ctx.mul(temp,a[j][i],s0[j][i]);
                    ctx.plus(sum, sum, temp);
				}
				
				 ole0[i]=sum;
			}
			
  });
  std::thread t1([&]() 
  {
    for(u64 i=0;i<c*c;i++)
	{
		prime_wht<F,Ctx>(cross_termB[i], length);
	}
    for(u64 i=0;i<c;i++)
	{
		prime_wht<F,Ctx>(s1[i], length);
	}
			
			for(u64 i=0;i<length;i++)
			{
				F temp,sum;
				ctx.zero(&sum, &sum+1);
				for(u64 j=0;j<c*c;j++)
				{

					ctx.mul(temp,a_cross[j][i], cross_termB[j][i]);
					ctx.plus(sum, temp, sum);
				}
				additive1[i]=sum;
				ctx.zero(&sum, &sum+1);
				for(u64 j=0;j<c;j++)
				{
					ctx.mul(temp,a[j][i],s1[j][i]);
                    ctx.plus(sum, sum, temp);
				}
				
				 ole1[i]=sum;
			}
			
			
  });
    
    t0.join();
    t1.join();
	double end=omp_get_wtime();
	//std::cout<<"n= "<<length<<" c="<<c<<" t="<<numPoints<<std::endl;
	std::cout<<"t="<<numPoints<<" c="<<c<<" length=2^"<<depth<<"  The total time of OLE  "<<double(end - start)<<" seconds"<<std::endl;
	std::cout<<"The total communication complexity is "<<comm_cost<<" MB"<<std::endl;
 for (u64 i = 0; i < length; ++i)
	{
		    F act, exp;
			ctx.plus(act, additive0[i], additive1[i]);
			ctx.mul(exp, ole0[i], ole1[i]);

			if (exp != act)
			{
				std::cout << "exp " << exp << std::endl;
				std::cout << "act " << act << std::endl;
			    std::cout<<i<<std::endl;
				
				throw RTE_LOC;
			}

		}   
}


template<typename F, typename Ctx>
void Prime_OLE(u64 depth,u64 block_length)
{
	PRNG prng(sysRandomSeed());
	u64 numPoints=ipow(2, block_length);
	u64 domain =ipow(2, depth-block_length);
	u64 length=ipow(2, depth);
	int num_points=numPoints*numPoints;
	std::vector<F> poly_points0(numPoints), poly_points1(numPoints),poly_pointe0(numPoints), poly_pointe1(numPoints);
	std::vector<std::vector<F>> cross_termA(4), cross_termB(4);
	std::vector<F>  poly_s0(numPoints), poly_s1(numPoints), poly_e0(numPoints), poly_e1(numPoints);
	std::vector<F> s0(length),s1(length),e0(length), e1(length), a(length), a_square(length);
	std::vector<F> additive0(length),additive1(length), ole0(length), ole1(length);
	Ctx ctx;
	
	for(u64 i=0;i<length;i++)
	{
		ctx.fromBlock(a[i],prng.get());
		ctx.mul(a_square[i],a[i],a[i]);
		
	}
	for(int i=0;i<4;i++)
	{
		cross_termA[i].resize(length);
		ctx.zero(cross_termA[i].begin(),cross_termA[i].end());
		cross_termB[i].resize(length);
		ctx.zero(cross_termB[i].begin(),cross_termB[i].end());
	}

	    ctx.zero(s0.begin(), s0.end());
		ctx.zero(s1.begin(), s1.end());
		ctx.zero(e0.begin(), e0.end());
		ctx.zero(e1.begin(), e1.end());
//	std::cout<<"Here0"<<std::endl;
	for (u64 i = 0; i < numPoints; ++i)
	{
		poly_points0[i] = prng.get<u64>()  % domain;
		poly_points1[i] = prng.get<u64>()  % domain;
		poly_pointe0[i] = prng.get<u64>()  % domain;
		poly_pointe1[i] = prng.get<u64>()  % domain;
		//std::cout << points[i] << " = " << points0[i] <<" + "<< points1[i] << std::endl;
		ctx.fromBlock(poly_s0[i], prng.get());
		ctx.fromBlock(poly_s1[i], prng.get());
		ctx.fromBlock(poly_e0[i], prng.get());
		ctx.fromBlock(poly_e1[i], prng.get());
		s0[poly_points0[i]+i*domain]=poly_s0[i];
		s1[poly_points1[i]+i*domain]=poly_s1[i];
		e0[poly_pointe0[i]+i*domain]=poly_e0[i];
		e1[poly_pointe1[i]+i*domain]=poly_e1[i];
		//ctx.minus(points0[i], points[i], points1[i];)
	}
	double start=omp_get_wtime();
	float comm_cost=0;
	comm_cost+=prime_dpf<F, Ctx>(poly_s0, poly_s1, poly_points0,poly_points1, cross_termA[0], cross_termB[0], depth, length, domain);
	comm_cost+=prime_dpf<F, Ctx>(poly_s0, poly_e1, poly_points0,poly_pointe1, cross_termA[1], cross_termB[1], depth, length, domain);
	comm_cost+=prime_dpf<F, Ctx>(poly_e0, poly_s1, poly_pointe0,poly_points1, cross_termA[2], cross_termB[2], depth, length, domain);
	comm_cost+=prime_dpf<F, Ctx>(poly_e0, poly_e1, poly_pointe0,poly_pointe1, cross_termA[3], cross_termB[3], depth, length, domain);
//	std::cout<<"Here1"<<std::endl;
  std::thread t0([&]() 
  {
	        prime_wht<F,Ctx>(cross_termA[0], length);
			prime_wht<F,Ctx>(cross_termA[1], length);
			prime_wht<F,Ctx>(cross_termA[2], length);
			prime_wht<F,Ctx>(cross_termA[3], length);
			prime_wht<F,Ctx>(s0, length);
			prime_wht<F,Ctx>(e0, length);			
			for(u64 i=0;i<length;i++)
			{
				F temp, sum;
				ctx.mul(sum,a_square[i],cross_termA[0][i]);
				ctx.mul(temp, a[i], cross_termA[1][i]);
                ctx.plus(sum,sum,temp);
				ctx.mul(temp, a[i], cross_termA[2][i]);
				ctx.plus(sum,sum,temp);
				ctx.plus(additive0[i], sum, cross_termA[3][i]);
				ctx.mul(temp,a[i],s0[i]);
				ctx.plus(ole0[i],e0[i],temp);
			}
			
  });
  std::thread t1([&]() 
  {
            prime_wht<F,Ctx>(cross_termB[0], length);
			prime_wht<F,Ctx>(cross_termB[1], length);
			prime_wht<F,Ctx>(cross_termB[2], length);
			prime_wht<F,Ctx>(cross_termB[3], length);
			prime_wht<F,Ctx>(s1, length);
			prime_wht<F,Ctx>(e1, length);			
			for(u64 i=0;i<length;i++)
			{
				F temp, sum;
				ctx.mul(sum,a_square[i],cross_termB[0][i]);
				ctx.mul(temp, a[i], cross_termB[1][i]);
                ctx.plus(sum,sum,temp);
				ctx.mul(temp, a[i], cross_termB[2][i]);
				ctx.plus(sum,sum,temp);
				ctx.plus(additive1[i], sum, cross_termB[3][i]);
				ctx.mul(temp,a[i],s1[i]);
				ctx.plus(ole1[i],e1[i],temp);
			}
			
  });
    
    t0.join();
    t1.join();
	double end=omp_get_wtime();
	std::cout<<"The total time of OLE "<<double(end - start)<<" seconds"<<std::endl;
	std::cout<<"The total communication complexity is "<<comm_cost<<" MB"<<std::endl;
 for (u64 i = 0; i < length; ++i)
	{
		    F act, exp;
			ctx.plus(act, additive0[i], additive1[i]);
			ctx.mul(exp, ole0[i], ole1[i]);

			if (exp != act)
			{
				std::cout << "exp " << exp << std::endl;
				std::cout << "act " << act << std::endl;
			    std::cout<<i<<std::endl;
				throw RTE_LOC;
			}

		}   
}
