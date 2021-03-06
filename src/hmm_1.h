// ======================================================================
// PureCLIP: capturing target-specific protein-RNA interaction footprints
// ======================================================================
// Copyright (C) 2017  Sabrina Krakau, Max Planck Institute for Molecular 
// Genetics
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// =======================================================================
// Author: Sabrina Krakau <krakau@molgen.mpg.de>
// =======================================================================



#ifndef APPS_HMMS_HMM_1_H_
#define APPS_HMMS_HMM_1_H_
  

#include <iostream>
#include <fstream>

#include "density_functions.h"
#include "density_functions_reg.h"
#include "density_functions_crosslink.h"
#include "density_functions_crosslink_reg.h"
#include <math.h>  

using namespace seqan;


template <typename TD1, typename TD2, typename TB1, typename TB2>
class HMM {     

public:

    __uint8                    K;                  // no. of sates
    String<String<String<double> > >   initProbs;          // intital probabilities

    String<String<Observations> >       & setObs;          // workaround for partial specialization
    String<String<double> >     transMatrix;

    HMM(int K_, String<String<Observations> > & setObs_): K(K_), setObs(setObs_)
    {
        // initialize transition probabilities
        resize(transMatrix, K, Exact());
        double trans1 = 0.6;    // increased probability to stay in same state
        for (unsigned i = 0; i < K; ++i)
        {
            resize(transMatrix[i], K, Exact());
            for (unsigned j = 0; j < K; ++j)
                if (i == j)
                    transMatrix[i][j] = trans1;
                else
                    transMatrix[i][j] = (1.0 - trans1) / (K - 1.0);
        }
       
        resize(initProbs, 2, Exact());
        resize(eProbs, 2, Exact());
        resize(statePosteriors, 2, Exact());
        for (unsigned s = 0; s < 2; ++s)
        {
            resize(initProbs[s], length(setObs[s]), Exact());
            resize(eProbs[s], length(setObs[s]), Exact());
            resize(statePosteriors[s], K, Exact());
            for (unsigned k = 0; k < K; ++k)
                resize(statePosteriors[s][k], length(setObs[s]), Exact());

            for (unsigned i = 0; i < length(setObs[s]); ++i)
            {
                // set initial probabilities to uniform
                resize(initProbs[s][i], K, Exact());
                for (unsigned k = 0; k < K; ++k)
                    initProbs[s][i][k] = 1.0 / K;

                unsigned T = setObs[s][i].length();
                resize(eProbs[s][i], T, Exact());
                for (unsigned k = 0; k < K; ++k)
                    resize(statePosteriors[s][k][i], T, Exact());

                for (unsigned t = 0; t < T; ++t)
                {
                    resize(eProbs[s][i][t], K, Exact());
                }
            }
        }
     }

    HMM<TD1, TD2, TB1, TB2>();
    ~HMM<TD1, TD2, TB1, TB2>();
    void setInitProbs(String<double> &probs);
    bool computeEmissionProbs(TD1 &d1, TD2 &d2, TB1 &bin1, TB2 &bin2, AppOptions &options);
    void iForward(String<String<double> > &alphas_1, String<String<double> > &alphas_2, unsigned s, unsigned i);
    //void forward_noSc();
    void iBackward(String<String<double> > &betas_2, String<String<double> > &alphas_1, unsigned s, unsigned i);
    //void backward_noSc();
    void computeStatePosteriorsFB(AppOptions &options);
    void computeStatePosteriorsFBupdateTrans(AppOptions &options);
    //void updateTransition(AppOptions &options);
    //void updateTransition2();
    //void updateTransition_noSc2();
    bool updateDensityParams(TD1 &d1, TD2 &d2, AppOptions &options);
    bool updateDensityParams(TD1 /*&d1*/, TD2 /*&d2*/, TB1 &bin1, TB2 &bin2, AppOptions &options);
    bool baumWelch(TD1 &d1, TD2 &d2, TB1 &bin1, TB2 &bin2, CharString learnTag, AppOptions &options);
    bool applyParameters(TD1 &d1, TD2 &d2, TB1 &bin1, TB2 &bin2, AppOptions &/*options*/);
    double viterbi(String<String<String<__uint8> > > &states);
    double viterbi_log(String<String<String<__uint8> > > &states);
    void posteriorDecoding(String<String<String<__uint8> > > &states);


    // for each F/R,interval,t, state ....
    String<String<String<String<double> > > > eProbs;           // emission/observation probabilities  P(Y_t | S_t) -> precompute for each t given Y_t = (C_t, T_t) !!!
    String<String<String<String<double> > > > statePosteriors;  // for each k: for each covered interval string of posteriors
};


template<typename TD1, typename TD2, typename TB1, typename TB2>
HMM<TD1, TD2, TB1, TB2>::~HMM<TD1, TD2, TB1, TB2>()
{
    clear(this->eProbs);
    clear(this->statePosteriors);
    clear(this->initProbs);
    clear(this->transMatrix);
   // do not touch observations
}


// assumes gamma, kdes
template<>
bool HMM<GAMMA2, GAMMA2, ZTBIN, ZTBIN>::computeEmissionProbs(GAMMA2 &d1, GAMMA2 &d2, ZTBIN &bin1, ZTBIN &bin2, AppOptions &options)
{
    bool stop = false;
    for (unsigned s = 0; s < 2; ++s)
    {
#if HMM_PARALLEL
        SEQAN_OMP_PRAGMA(parallel for schedule(dynamic, 1)) 
#endif  
        for (unsigned i = 0; i < length(this->setObs[s]); ++i)
        {
            for (unsigned t = 0; t < this->setObs[s][i].length(); ++t)   // TODO getDensity() use functor!!!
            {
                if (this->setObs[s][i].kdes[t] == 0.0)
                {
                    std::cerr << "ERROR: KDE is 0.0 on forward strand at i " << i << " t: " << t << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    stop = true;
                }
                double g1_d = 1.0;
                double g2_d = 0.0;
                if (this->setObs[s][i].kdes[t] >= d1.tp) 
                {
                    g1_d = d1.getDensity(this->setObs[s][i].kdes[t]);
                    g2_d = d2.getDensity(this->setObs[s][i].kdes[t]); 
                }
                double bin1_d = 1.0;
                double bin2_d = 0.0;
                if (this->setObs[s][i].truncCounts[t] > 0)
                {
                    bin1_d = bin1.getDensity(this->setObs[s][i].truncCounts[t], this->setObs[s][i].nEstimates[t]);
                    bin2_d = bin2.getDensity(this->setObs[s][i].truncCounts[t], this->setObs[s][i].nEstimates[t]);
                }
                this->eProbs[s][i][t][0] = g1_d * bin1_d;    
                this->eProbs[s][i][t][1] = g1_d * bin2_d;
                this->eProbs[s][i][t][2] = g2_d * bin1_d;
                this->eProbs[s][i][t][3] = g2_d * bin2_d;
  
                // debug
                if (this->eProbs[s][i][t][0] == 0 && this->eProbs[s][i][t][1] == 0 && this->eProbs[s][i][t][2] == 0 && this->eProbs[s][i][t][3] == 0)
                {
                    std::cout << "ERROR: all emission probabilities are 0!" << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       fragment coverage (kde): " << this->setObs[s][i].kdes[t] << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       read start count: " << (int)this->setObs[s][i].truncCounts[t] << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       estimated n: " << this->setObs[s][i].nEstimates[t] << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       emission probability 'non-enriched' gamma: " << d1.getDensity(this->setObs[s][i].kdes[t]) << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       emission probability 'enriched' gamma: " << d2.getDensity(this->setObs[s][i].kdes[t]) << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       emission probability 'non-crosslink' binomial: " << bin1.getDensity(this->setObs[s][i].truncCounts[t], this->setObs[s][i].nEstimates[t]) << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       emission probability 'crosslink' binomial: " << bin2.getDensity(this->setObs[s][i].truncCounts[t], this->setObs[s][i].nEstimates[t]) << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "Try to learn on more or better representing chromosomes." << std::endl; 
                    SEQAN_OMP_PRAGMA(critical) 
                    if(options.verbosity != 3) stop = true;
               }
            }
        }
    }
    if (stop) return false;
    return true;
}

template<>
bool HMM<GAMMA2, GAMMA2, ZTBIN_REG, ZTBIN_REG>::computeEmissionProbs(GAMMA2 &d1, GAMMA2 &d2, ZTBIN_REG &bin1, ZTBIN_REG &bin2, AppOptions &options)
{
    bool stop = false;
    for (unsigned s = 0; s < 2; ++s)
    {
#if HMM_PARALLEL
        SEQAN_OMP_PRAGMA(parallel for schedule(dynamic, 1)) 
#endif  
        for (unsigned i = 0; i < length(this->setObs[s]); ++i)
        {
            for (unsigned t = 0; t < this->setObs[s][i].length(); ++t)   // TODO getDensity() use functor!!!
            {
                if (this->setObs[s][i].kdes[t] == 0.0)
                {
                    std::cerr << "ERROR: KDE is 0.0 on forward strand at i " << i << " t: " << t << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    stop = true;
                }
                double g1_d = 1.0;
                double g2_d = 0.0;
                if (this->setObs[s][i].kdes[t] >= d1.tp) 
                {
                    g1_d = d1.getDensity(this->setObs[s][i].kdes[t]);
                    g2_d = d2.getDensity(this->setObs[s][i].kdes[t]); 
                }
                unsigned mId = setObs[s][i].motifIds[t];
                double bin1_pred = 1.0/(1.0+exp(-bin1.b0 - bin1.regCoeffs[mId]*setObs[s][i].fimoScores[t]));
                double bin2_pred = 1.0/(1.0+exp(-bin2.b0 - bin2.regCoeffs[mId]*setObs[s][i].fimoScores[t]));

                double bin1_d = 1.0;
                double bin2_d = 0.0;
                if (this->setObs[s][i].truncCounts[t] > 0)
                {
                    bin1_d = bin1.getDensity(this->setObs[s][i].truncCounts[t], this->setObs[s][i].nEstimates[t], bin1_pred);
                    bin2_d = bin2.getDensity(this->setObs[s][i].truncCounts[t], this->setObs[s][i].nEstimates[t], bin2_pred);
                }
                this->eProbs[s][i][t][0] = g1_d * bin1_d;    
                this->eProbs[s][i][t][1] = g1_d * bin2_d;
                this->eProbs[s][i][t][2] = g2_d * bin1_d;
                this->eProbs[s][i][t][3] = g2_d * bin2_d;
  
                // debug
                if (this->eProbs[s][i][t][0] == 0 && this->eProbs[s][i][t][1] == 0 && this->eProbs[s][i][t][2] == 0 && this->eProbs[s][i][t][3] == 0)
                {
                    std::cout << "ERROR: all emission probabilities are 0!" << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       fragment coverage (kde): " << this->setObs[s][i].kdes[t] << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       read start count: " << (int)this->setObs[s][i].truncCounts[t] << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       estimated n: " << this->setObs[s][i].nEstimates[t] << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       covariate x: " << setObs[s][i].fimoScores[t] << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       emission probability 'non-enriched' gamma: " << d1.getDensity(this->setObs[s][i].kdes[t]) << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       emission probability 'enriched' gamma: " << d2.getDensity(this->setObs[s][i].kdes[t])  << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       emission probability 'non-crosslink' binomial: " << bin1.getDensity(this->setObs[s][i].truncCounts[t], this->setObs[s][i].nEstimates[t], bin1_pred) << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       emission probability 'crosslink' binomial: " << bin2.getDensity(this->setObs[s][i].truncCounts[t], this->setObs[s][i].nEstimates[t], bin2_pred) << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "Try to learn on more or better representing chromosomes." << std::endl; 
                    SEQAN_OMP_PRAGMA(critical) 
                    if(options.verbosity != 3) stop = true;
               }
            }
        }
    }
    if (stop) return false;
    return true;
}

// assumes gamma regression model, kdes
template<>
bool HMM<GAMMA2_REG, GAMMA2_REG, ZTBIN, ZTBIN>::computeEmissionProbs(GAMMA2_REG &d1, GAMMA2_REG &d2, ZTBIN &bin1, ZTBIN &bin2, AppOptions &options)
{
    bool stop = false;
    for (unsigned s = 0; s < 2; ++s)
    {
#if HMM_PARALLEL
        SEQAN_OMP_PRAGMA(parallel for schedule(dynamic, 1)) 
#endif  
        for (unsigned i = 0; i < length(this->setObs[s]); ++i)
        {
            for (unsigned t = 0; t < this->setObs[s][i].length(); ++t)   // todo getDensity() use functor!!!
            {
                double x = std::max(this->setObs[s][i].rpkms[t], options.minRPKMtoFit);
                if (this->setObs[s][i].kdes[t] == 0.0) 
                {
                    std::cerr << "ERROR: KDE is 0.0 on forward strand at i " << i << " t: " << t << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    stop = true;
                }
                double d1_pred = exp(d1.b0 + d1.b1 * x);
                double d2_pred = exp(d2.b0 + d2.b1 * x);

                double g1_d = 1.0;
                double g2_d = 0.0;
                if (this->setObs[s][i].kdes[t] >= d1.tp) 
                {
                    g1_d = d1.getDensity(this->setObs[s][i].kdes[t], d1_pred);
                    g2_d = d2.getDensity(this->setObs[s][i].kdes[t], d2_pred); 
                }
                double bin1_d = 1.0;
                double bin2_d = 0.0;
                if (this->setObs[s][i].truncCounts[t] > 0)
                {
                    bin1_d = bin1.getDensity(this->setObs[s][i].truncCounts[t], this->setObs[s][i].nEstimates[t]);
                    bin2_d = bin2.getDensity(this->setObs[s][i].truncCounts[t], this->setObs[s][i].nEstimates[t]);
                }
                this->eProbs[s][i][t][0] = g1_d * bin1_d;    
                this->eProbs[s][i][t][1] = g1_d * bin2_d;
                this->eProbs[s][i][t][2] = g2_d * bin1_d;
                this->eProbs[s][i][t][3] = g2_d * bin2_d;

                // debug
                if ((this->eProbs[s][i][t][0] == 0 && this->eProbs[s][i][t][1] == 0 && this->eProbs[s][i][t][2] == 0 && this->eProbs[s][i][t][3] == 0) || 
                        (std::isnan(this->eProbs[s][i][t][0]) || std::isnan(this->eProbs[s][i][t][1]) || std::isnan(this->eProbs[s][i][t][2]) || std::isnan(this->eProbs[s][i][t][3])) ||
                        (std::isinf(this->eProbs[s][i][t][0]) || std::isinf(this->eProbs[s][i][t][1]) || std::isinf(this->eProbs[s][i][t][2]) || std::isinf(this->eProbs[s][i][t][3])))
                {
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "ERROR: all emission probabilities are 0!" << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       fragment coverage (kde): " << this->setObs[s][i].kdes[t] << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       read start count: " << (int)this->setObs[s][i].truncCounts[t] << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       estimated n: " << this->setObs[s][i].nEstimates[t] << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       covariate b: " << x << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       emission probability 'non-enriched' gamma: " << d1.getDensity(this->setObs[s][i].kdes[t], d1_pred) << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       emission probability 'enriched' gamma: " << d2.getDensity(this->setObs[s][i].kdes[t], d2_pred) << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       emission probability 'non-crosslink' binomial: " << bin1.getDensity(this->setObs[s][i].truncCounts[t], this->setObs[s][i].nEstimates[t]) << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       emission probability 'crosslink' binomial: " << bin2.getDensity(this->setObs[s][i].truncCounts[t], this->setObs[s][i].nEstimates[t]) << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "Try to learn on more or better representing chromosomes." << std::endl; 
                    SEQAN_OMP_PRAGMA(critical) 
                    if(options.verbosity != 3) stop = true;
                }
            }
        }
    }
    if (stop) return false;
    return true;
}


template<>
bool HMM<GAMMA2_REG, GAMMA2_REG, ZTBIN_REG, ZTBIN_REG>::computeEmissionProbs(GAMMA2_REG &d1, GAMMA2_REG &d2, ZTBIN_REG &bin1, ZTBIN_REG &bin2, AppOptions &options)
{
    bool stop = false;
    for (unsigned s = 0; s < 2; ++s)
    {
#if HMM_PARALLEL
        SEQAN_OMP_PRAGMA(parallel for schedule(dynamic, 1)) 
#endif  
        for (unsigned i = 0; i < length(this->setObs[s]); ++i)
        {
            for (unsigned t = 0; t < this->setObs[s][i].length(); ++t)   // todo getDensity() use functor!!!
            {
                double x = std::max(this->setObs[s][i].rpkms[t], options.minRPKMtoFit);
                if (this->setObs[s][i].kdes[t] == 0.0) 
                {
                    std::cerr << "ERROR: KDE is 0.0 on forward strand at i " << i << " t: " << t << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    stop = true;
                }
                // gammas
                double d1_pred = exp(d1.b0 + d1.b1 * x);
                double d2_pred = exp(d2.b0 + d2.b1 * x);
                double g1_d = 1.0;
                double g2_d = 0.0;
                if (this->setObs[s][i].kdes[t] >= d1.tp) 
                {
                    g1_d = d1.getDensity(this->setObs[s][i].kdes[t], d1_pred);
                    g2_d = d2.getDensity(this->setObs[s][i].kdes[t], d2_pred); 
                }

                // binomials
                unsigned mId = setObs[s][i].motifIds[t];
                double bin1_pred = 1.0/(1.0+exp(-bin1.b0 - bin1.regCoeffs[mId]*setObs[s][i].fimoScores[t]));
                double bin2_pred = 1.0/(1.0+exp(-bin2.b0 - bin2.regCoeffs[mId]*setObs[s][i].fimoScores[t]));
                double bin1_d = 1.0;
                double bin2_d = 0.0;
                if (this->setObs[s][i].truncCounts[t] > 0)
                {
                    bin1_d = bin1.getDensity(this->setObs[s][i].truncCounts[t], this->setObs[s][i].nEstimates[t], bin1_pred);
                    bin2_d = bin2.getDensity(this->setObs[s][i].truncCounts[t], this->setObs[s][i].nEstimates[t], bin2_pred);
                }

                this->eProbs[s][i][t][0] = g1_d * bin1_d;    
                this->eProbs[s][i][t][1] = g1_d * bin2_d;
                this->eProbs[s][i][t][2] = g2_d * bin1_d;
                this->eProbs[s][i][t][3] = g2_d * bin2_d;

                // debug
                if ((this->eProbs[s][i][t][0] == 0 && this->eProbs[s][i][t][1] == 0 && this->eProbs[s][i][t][2] == 0 && this->eProbs[s][i][t][3] == 0) || 
                        (std::isnan(this->eProbs[s][i][t][0]) || std::isnan(this->eProbs[s][i][t][1]) || std::isnan(this->eProbs[s][i][t][2]) || std::isnan(this->eProbs[s][i][t][3])) ||
                        (std::isinf(this->eProbs[s][i][t][0]) || std::isinf(this->eProbs[s][i][t][1]) || std::isinf(this->eProbs[s][i][t][2]) || std::isinf(this->eProbs[s][i][t][3])))
                {
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "ERROR: all emission probabilities are 0!" << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       fragment coverage (kde): " << this->setObs[s][i].kdes[t] << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       read start count: " << (int)this->setObs[s][i].truncCounts[t] << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       estimated n: " << this->setObs[s][i].nEstimates[t] << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       covariate b: " << x << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       covariate x: " << setObs[s][i].fimoScores[t] << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       emission probability 'non-enriched' gamma: " << d1.getDensity(this->setObs[s][i].kdes[t], d1_pred) << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       emission probability 'enriched' gamma: " << d2.getDensity(this->setObs[s][i].kdes[t], d2_pred) << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       emission probability 'non-crosslink' binomial: " << bin1.getDensity(this->setObs[s][i].truncCounts[t], this->setObs[s][i].nEstimates[t], bin1_pred) << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "       emission probability 'crosslink' binomial: " << bin2.getDensity(this->setObs[s][i].truncCounts[t], this->setObs[s][i].nEstimates[t], bin2_pred) << std::endl;
                    SEQAN_OMP_PRAGMA(critical) 
                    std::cout << "Try to learn on more or better representing chromosomes." << std::endl; 
                    SEQAN_OMP_PRAGMA(critical) 
                    if(options.verbosity != 3) stop = true;
                }
            }
        }
    }
    if (stop) return false;
    return true;
}
 
// Forward-backward algorithm 
// without scaling for testing 
/*template<typename TD1, typename TD2, typename TB1, typename TB2>
void HMM<TD1, TD2, TB1, TB2>::forward_noSc()
{
    for (unsigned s = 0; s < 2; ++s)
    {
        // for t = 1
        for (unsigned i = 0; i < length(this->setObs[s]); ++i)
        {
            for (unsigned k = 0; k < this->K; ++k)
                this->alphas_2[s][i][0][k] = this->initProbs[s][i][k] * this->eProbs[s][i][0][k];
        
            // for t = 2:T
            for (unsigned t = 1; t < this->setObs[s][i].length(); ++t)
            {
                for (unsigned k = 0; k < this->K; ++k)
                {
                    // sum over previous states
                    double sum = 0.0;
                    for (unsigned k_2 = 0; k_2 < this->K; ++k_2)
                        sum += alphas_2[s][i][t-1][k_2] * this->transMatrix[k_2][k];

                    this->alphas_2[s][i][t][k] = sum * this->eProbs[s][i][t][k];
                }
            }
        }
    }
}

//// probabilities getting small, in a naive implementation alpha and beta will underflow
//// -> scaling technique (more stable and faster than log method ?)
// compute scaled alphas for each state and corresponding c values
template<typename TD1, typename TD2, typename TB1, typename TB2>
void HMM<TD1, TD2, TB1, TB2>::forward()
{
    for (unsigned s = 0; s < 2; ++s)
    {
#if HMM_PARALLEL
        SEQAN_OMP_PRAGMA(parallel for schedule(dynamic, 1)) 
#endif  
        for (unsigned i = 0; i < length(this->setObs[s]); ++i)
        {
            // for t = 1
            double norm = 0.0;
            for (unsigned k = 0; k < this->K; ++k)
            {
                this->alphas_1[s][i][0][k] = this->initProbs[s][i][k] * this->eProbs[s][i][0][k];
                norm += this->alphas_1[s][i][0][k];
            }
            if (norm == 0.0) 
            {
                std::cerr << "ERROR: norm == 0 at t: "<< 0 << "  i: " << i << std::endl;
                for (unsigned k = 0; k < this->K; ++k)
                {
                    std::cout << "k: " << k << std::endl;
                    std::cout << "this->eProbs[s][i][0][k] " << this->eProbs[s][i][0][k] << std::endl;
                }
            }

            for (unsigned k = 0; k < this->K; ++k)
               this->alphas_2[s][i][0][k] = alphas_1[s][i][0][k] / norm;

            // for t = 2:T
            for (unsigned t = 1; t < this->setObs[s][i].length(); ++t)
            {
                norm = 0.0;
                for (unsigned k = 0; k < this->K; ++k)
                {
                    // sum over previous states
                    double sum = 0.0;
                    for (unsigned k_2 = 0; k_2 < this->K; ++k_2)
                        sum += alphas_2[s][i][t-1][k_2] * this->transMatrix[k_2][k];
                    
                    if (sum == 0.0 || std::isnan(sum)) 
                    {
                        std::cerr << "ERROR: sum = " << norm << " at t: "<< t << "  i: " << i << std::endl;
                        for (unsigned k_2 = 0; k_2 < this->K; ++k_2)
                            std::cout << " k_2: " << k_2 <<  " alphas_2[s][i][t-1][k_2]: " << alphas_2[s][i][t-1][k_2] << " transMatrix[k_2][k]: " << this->transMatrix[k_2][k] << std::endl; 
                    }

                    // alpha_1
                    this->alphas_1[s][i][t][k] = sum * this->eProbs[s][i][t][k];        // - nan?
                    norm += this->alphas_1[s][i][t][k];
                }
                
                if (norm == 0.0 || std::isnan(norm)) 
                {
                    std::cerr << "ERROR: norm = " << norm << " at t: "<< t << "  i: " << i << std::endl;
                    for (unsigned k = 0; k < this->K; ++k)
                    {
                        std::cout << "k: " << k << std::endl;
                        std::cout << "this->eProbs[s][i][t][k] " << this->eProbs[s][i][t][k] << std::endl;
                    }
                }
                // normalize
                for (unsigned k = 0; k < this->K; ++k)
                    this->alphas_2[s][i][t][k] = alphas_1[s][i][t][k] / norm;   // TODO store scaling coefficients too !?
            }
        }
    }
}*/


// for one interval only
template<typename TD1, typename TD2, typename TB1, typename TB2>
void HMM<TD1, TD2, TB1, TB2>::iForward(String<String<double> > &alphas_1, String<String<double> > &alphas_2, unsigned s, unsigned i)
{
    // for t = 1
    double norm = 0.0;
    for (unsigned k = 0; k < this->K; ++k)
    {
        alphas_1[0][k] = this->initProbs[s][i][k] * this->eProbs[s][i][0][k];
        norm += alphas_1[0][k];
    }
    if (norm == 0.0) 
    {
        std::cerr << "ERROR: norm == 0 at t: "<< 0 << "  i: " << i << std::endl;
        for (unsigned k = 0; k < this->K; ++k)
        {
            std::cout << "k: " << k << std::endl;
            std::cout << "this->eProbs[s][i][0][k] " << this->eProbs[s][i][0][k] << std::endl;
        }
    }

    for (unsigned k = 0; k < this->K; ++k)
       alphas_2[0][k] = alphas_1[0][k] / norm;

    // for t = 2:T
    for (unsigned t = 1; t < this->setObs[s][i].length(); ++t)
    {
        norm = 0.0;
        for (unsigned k = 0; k < this->K; ++k)
        {
            // sum over previous states
            double sum = 0.0;
            for (unsigned k_2 = 0; k_2 < this->K; ++k_2)
                sum += alphas_2[t-1][k_2] * this->transMatrix[k_2][k];
            
            if (sum == 0.0 || std::isnan(sum)) 
            {
                std::cerr << "ERROR: sum = " << norm << " at t: "<< t << "  i: " << i << std::endl;
                for (unsigned k_2 = 0; k_2 < this->K; ++k_2)
                    std::cout << " k_2: " << k_2 <<  " alphas_2[t-1][k_2]: " << alphas_2[t-1][k_2] << " transMatrix[k_2][k]: " << this->transMatrix[k_2][k] << std::endl; 
            }

            // alpha_1
            alphas_1[t][k] = sum * this->eProbs[s][i][t][k];        // - nan?
            norm += alphas_1[t][k];
        }
        
        if (norm == 0.0 || std::isnan(norm)) 
        {
            std::cerr << "ERROR: norm = " << norm << " at t: "<< t << "  i: " << i << std::endl;
            for (unsigned k = 0; k < this->K; ++k)
            {
                std::cout << "k: " << k << std::endl;
                std::cout << "this->eProbs[s][i][t][k] " << this->eProbs[s][i][t][k] << std::endl;
            }
        }
        // normalize
        for (unsigned k = 0; k < this->K; ++k)
            alphas_2[t][k] = alphas_1[t][k] / norm;   // TODO store scaling coefficients too !?
    }
}


// Backward-algorithm
// without scaling for testing 
/*template<typename TD1, typename TD2, typename TB1, typename TB2>
void HMM<TD1, TD2, TB1, TB2>::backward_noSc()
{
    for (unsigned s = 0; s < 2; ++s)
    {
        for (unsigned i = 0; i < length(this->setObs[s]); ++i)
        {
            // for t = T
            for (unsigned k = 0; k < this->K; ++k)
               this->betas_2[s][i][this->setObs[s][i].length() - 1][k] = 1.0;
            
            // for t = 2:T
            for (int t = this->setObs[s][i].length() - 2; t >= 0; --t)
            {
                for (unsigned k = 0; k < this->K; ++k)
                {
                    // sum over previous states
                    double sum = 0.0;
                    for (unsigned k_2 = 0; k_2 < this->K; ++k_2)
                        sum += betas_2[s][i][t+1][k_2] * this->transMatrix[k][k_2] * this->eProbs[s][i][t+1][k_2];
                    // beta_1
                    this->betas_2[s][i][t][k] = sum;
                }
            }
        }
    }
}

// with scaling method
template<typename TD1, typename TD2, typename TB1, typename TB2>
void HMM<TD1, TD2, TB1, TB2>::backward()
{
    for (unsigned s = 0; s < 2; ++s)
    {
#if HMM_PARALLEL
        SEQAN_OMP_PRAGMA(parallel for schedule(dynamic, 1)) 
#endif  
        for (unsigned i = 0; i < length(this->setObs[s]); ++i)
        {
            // for t = T
            for (unsigned k = 0; k < this->K; ++k)
               this->betas_1[s][i][this->setObs[s][i].length() - 1][k] = 1.0;
            
            double norm = 0.0;      // use scaling coefficients from alphas here !
            for (unsigned k = 0; k < this->K; ++k)
               norm += this->alphas_1[s][i][this->setObs[s][i].length() - 1][k];

            for (unsigned k = 0; k < this->K; ++k)
               this->betas_2[s][i][this->setObs[s][i].length() - 1][k] = betas_1[s][i][this->setObs[s][i].length() - 1][k] / norm;

            // for t = 2:T
            for (int t = this->setObs[s][i].length() - 2; t >= 0; --t)
            {
                norm = 0.0;
                for (unsigned k = 0; k < this->K; ++k)      // precompute ???
                    norm += this->alphas_1[s][i][t][k];

                for (unsigned k = 0; k < this->K; ++k)
                {
                    // sum over previous states
                    double sum = 0.0;
                    for (unsigned k_2 = 0; k_2 < this->K; ++k_2)
                        sum += betas_2[s][i][t+1][k_2] * this->transMatrix[k][k_2] * this->eProbs[s][i][t+1][k_2];
                    
                    // beta_1
                    this->betas_1[s][i][t][k] = sum;
                    // beta_2
                    this->betas_2[s][i][t][k] = this->betas_1[s][i][t][k] / norm;
                }
            }
        }
    }
}*/


// need alphas_1 for scaling here,
// only betas_2 is needed to compute posterior probs.
template<typename TD1, typename TD2, typename TB1, typename TB2>
void HMM<TD1, TD2, TB1, TB2>::iBackward(String<String<double> > &betas_2, String<String<double> > &alphas_1, unsigned s, unsigned i)
{
    unsigned T = this->setObs[s][i].length();
    String<String<double> > betas_1;
    resize(betas_1, T, Exact());
    for (unsigned t = 0; t < T; ++t)
        resize(betas_1[t], this->K, Exact());

    // for t = T
    for (unsigned k = 0; k < this->K; ++k)
       betas_1[this->setObs[s][i].length() - 1][k] = 1.0;
    
    double norm = 0.0;      // use scaling coefficients from alphas here !
    for (unsigned k = 0; k < this->K; ++k)
       norm += alphas_1[this->setObs[s][i].length() - 1][k];

    for (unsigned k = 0; k < this->K; ++k)
       betas_2[this->setObs[s][i].length() - 1][k] = betas_1[this->setObs[s][i].length() - 1][k] / norm;

    // for t = 2:T
    for (int t = this->setObs[s][i].length() - 2; t >= 0; --t)
    {
        norm = 0.0;
        for (unsigned k = 0; k < this->K; ++k)      // precompute ???
            norm += alphas_1[t][k];

        for (unsigned k = 0; k < this->K; ++k)
        {
            // sum over previous states
            double sum = 0.0;
            for (unsigned k_2 = 0; k_2 < this->K; ++k_2)
                sum += betas_2[t+1][k_2] * this->transMatrix[k][k_2] * this->eProbs[s][i][t+1][k_2];
            
            // beta_1
            betas_1[t][k] = sum;
            // beta_2
            betas_2[t][k] = betas_1[t][k] / norm;
        }
    }
}


// both for scaling and no-scaling method
/*template<typename TD1, typename TD2, typename TB1, typename TB2>
void HMM<TD1, TD2, TB1, TB2>::computeStatePosteriors()
{
    for (unsigned s = 0; s < 2; ++s)
    {
#if HMM_PARALLEL
        SEQAN_OMP_PRAGMA(parallel for schedule(dynamic, 1)) 
#endif  
        for (unsigned i = 0; i < length(this->setObs[s]); ++i)
        {
            for (unsigned t = 0; t < this->setObs[s][i].length(); ++t)
            {
                double sum = 0.0;
                for (unsigned k = 0; k < this->K; ++k)
                    sum += this->alphas_2[s][i][t][k] * this->betas_2[s][i][t][k];

                if (sum == 0.0) 
                {
                    std::cerr << "ERROR: sum == 0 at i: " << i << " t: "<< t << std::endl;
                    for (unsigned k = 0; k < this->K; ++k)
                    {
                        std::cout << "k: " << k << std::endl;
                        std::cout << "this->alphas_2[s][i][t][k]: " << this->alphas_2[s][i][t][k] << " this->betas_2[s][i][t][k]: " << this->betas_2[s][i][t][k] << std::endl;
                    }
                }
                for (unsigned k = 0; k < this->K; ++k)
                    this->statePosteriors[s][k][i][t] = this->alphas_2[s][i][t][k] * this->betas_2[s][i][t][k] / sum;
            }
        }
    }
}*/




// for scaling method
// interval-wise to avoid storing alpha_1, alpha_2 and beta_1, beta_2 values for whole genome
template<typename TD1, typename TD2, typename TB1, typename TB2>
void HMM<TD1, TD2, TB1, TB2>::computeStatePosteriorsFBupdateTrans(AppOptions &options)
{
    String<String<double> > A = this->transMatrix;
    String<String<double> > p;
    resize(p, this->K, Exact());
    for (unsigned k_1 = 0; k_1 < this->K; ++k_1)    
    {
        SEQAN_OMP_PRAGMA(critical)
        resize(p[k_1], this->K, Exact());
        for (unsigned k_2 = 0; k_2 < this->K; ++k_2)
            p[k_1][k_2] = 0.0;
    }

    for (unsigned s = 0; s < 2; ++s)
    {
#if HMM_PARALLEL
        SEQAN_OMP_PRAGMA(parallel for schedule(dynamic, 1)) 
#endif  
        for (unsigned i = 0; i < length(this->setObs[s]); ++i)
        {
            unsigned T = setObs[s][i].length();
            // forward probabilities
            String<String<double> > alphas_1;
            String<String<double> > alphas_2;
            resize(alphas_1, T, Exact());
            resize(alphas_2, T, Exact());
            for (unsigned t = 0; t < T; ++t)
            {
                resize(alphas_1[t], this->K, Exact());
                resize(alphas_2[t], this->K, Exact());
            } 
            iForward(alphas_1, alphas_2, s, i);

            // backward probabilities  
            String<String<double> > betas_2;
            resize(betas_2, T, Exact());
            for (unsigned t = 0; t < T; ++t)
                resize(betas_2[t], this->K, Exact());
            iBackward(betas_2, alphas_1, s, i);
            
            // compute state posterior probabilities
            for (unsigned t = 0; t < this->setObs[s][i].length(); ++t)
            {
                double sum = 0.0;
                for (unsigned k = 0; k < this->K; ++k)
                    sum += alphas_2[t][k] * betas_2[t][k];

                if (sum == 0.0) 
                {
                    std::cerr << "ERROR: sum == 0 at i: " << i << " t: "<< t << std::endl;
                    for (unsigned k = 0; k < this->K; ++k)
                    {
                        std::cout << "k: " << k << std::endl;
                        std::cout << "alphas_2[k]: " << alphas_2[t][k] << " betas_2[t][k]: " << betas_2[t][k] << std::endl;
                    }
                }
                for (unsigned k = 0; k < this->K; ++k)
                    this->statePosteriors[s][k][i][t] = alphas_2[t][k] * betas_2[t][k] / sum;
            }

            // update init probs
            for (unsigned k = 0; k < this->K; ++k)
                this->initProbs[s][i][k] = this->statePosteriors[s][k][i][0];   

            // compute new transitioon probs
            String<String<double> > p_i;
            resize(p_i, this->K, Exact());
            for (unsigned k_1 = 0; k_1 < this->K; ++k_1)    
            {
                resize(p_i[k_1], this->K, Exact());
                for (unsigned k_2 = 0; k_2 < this->K; ++k_2)
                {
                    p_i[k_1][k_2] = 0.0;
                    for (unsigned t = 1; t < this->setObs[s][i].length(); ++t)
                    {
                        p_i[k_1][k_2] += alphas_2[t-1][k_1] * this->transMatrix[k_1][k_2] * this->eProbs[s][i][t][k_2] * betas_2[t][k_2]; 
                    }
                    SEQAN_OMP_PRAGMA(critical)
                    p[k_1][k_2] += p_i[k_1][k_2];
                }
            }

        }
    }
    // update transition matrix
    for (unsigned k_1 = 0; k_1 < this->K; ++k_1)
    {
        double denumerator = 0.0;
        for (unsigned k_3 = 0; k_3 < this->K; ++k_3)
        {
            denumerator += p[k_1][k_3]; 
        }
        for (unsigned k_2 = 0; k_2 < this->K; ++k_2)
        {
            A[k_1][k_2] = p[k_1][k_2] / denumerator;

            if (A[k_1][k_2] <= 0.0) 
                A[k_1][k_2] = DBL_MIN;          // make sure not getting zero
        }
    }
    // keep transProb of '2' -> '3' on min. value
    if (A[2][3] < options.minTransProbCS)
    {
        A[2][3] = options.minTransProbCS;

        if (A[3][3] < options.minTransProbCS)
            A[3][3] = options.minTransProbCS;
        std::cout << "NOTE: Prevented transition probability '2' -> '3' from dropping below min. value of " << options.minTransProbCS << ". Set for transitions '2' -> '3' (and if necessary also for '3'->'3') to " << options.minTransProbCS << "." << std::endl;
    }
    this->transMatrix = A;
}

// without updating transition probabilities 
template<typename TD1, typename TD2, typename TB1, typename TB2>
void HMM<TD1, TD2, TB1, TB2>::computeStatePosteriorsFB(AppOptions &options)
{
    String<String<double> > A = this->transMatrix;
    String<String<double> > p;
    resize(p, this->K, Exact());
    for (unsigned k_1 = 0; k_1 < this->K; ++k_1)    
    {
        SEQAN_OMP_PRAGMA(critical)
        resize(p[k_1], this->K, Exact());
        for (unsigned k_2 = 0; k_2 < this->K; ++k_2)
            p[k_1][k_2] = 0.0;
    }

    for (unsigned s = 0; s < 2; ++s)
    {
#if HMM_PARALLEL
        SEQAN_OMP_PRAGMA(parallel for schedule(dynamic, 1)) 
#endif  
        for (unsigned i = 0; i < length(this->setObs[s]); ++i)
        {
            unsigned T = setObs[s][i].length();
            // forward probabilities
            String<String<double> > alphas_1;
            String<String<double> > alphas_2;
            resize(alphas_1, T, Exact());
            resize(alphas_2, T, Exact());
            for (unsigned t = 0; t < T; ++t)
            {
                resize(alphas_1[t], this->K, Exact());
                resize(alphas_2[t], this->K, Exact());
            } 
            iForward(alphas_1, alphas_2, s, i);

            // backward probabilities  
            String<String<double> > betas_2;
            resize(betas_2, T, Exact());
            for (unsigned t = 0; t < T; ++t)
                resize(betas_2[t], this->K, Exact());
            iBackward(betas_2, alphas_1, s, i);
            
            // compute state posterior probabilities
            for (unsigned t = 0; t < this->setObs[s][i].length(); ++t)
            {
                double sum = 0.0;
                for (unsigned k = 0; k < this->K; ++k)
                    sum += alphas_2[t][k] * betas_2[t][k];

                if (sum == 0.0) 
                {
                    std::cerr << "ERROR: sum == 0 at i: " << i << " t: "<< t << std::endl;
                    for (unsigned k = 0; k < this->K; ++k)
                    {
                        std::cout << "k: " << k << std::endl;
                        std::cout << "alphas_2[k]: " << alphas_2[t][k] << " betas_2[t][k]: " << betas_2[t][k] << std::endl;
                    }
                }
                for (unsigned k = 0; k < this->K; ++k)
                    this->statePosteriors[s][k][i][t] = alphas_2[t][k] * betas_2[t][k] / sum;
            }

            // update init probs
            for (unsigned k = 0; k < this->K; ++k)
                this->initProbs[s][i][k] = this->statePosteriors[s][k][i][0];   
        }
    }
}


// both for scaling and no-scaling
/*template<typename TD1, typename TD2, typename TB1, typename TB2>
void HMM<TD1, TD2, TB1, TB2>::updateTransition(AppOptions &options)    // precompute numerator, denumerator, for each k_1, k_2 combination!!!
{
    String<String<double> > A = this->transMatrix;
    String<String<double> > p;
    resize(p, this->K, Exact());

#if HMM_PARALLEL
    SEQAN_OMP_PRAGMA(parallel for schedule(dynamic, 1)) 
#endif
    for (unsigned k_1 = 0; k_1 < this->K; ++k_1)    
    {
        SEQAN_OMP_PRAGMA(critical)
        resize(p[k_1], this->K, Exact());

        for (unsigned k_2 = 0; k_2 < this->K; ++k_2)
        {
            p[k_1][k_2] = 0.0;
            for (unsigned s = 0; s < 2; ++s)
                for (unsigned i = 0; i < length(this->setObs[s]); ++i)
                    for (unsigned t = 1; t < this->setObs[s][i].length(); ++t)
                        p[k_1][k_2] += this->alphas_2[s][i][t-1][k_1] * this->transMatrix[k_1][k_2] *  this->eProbs[s][i][t][k_2] * betas_2[s][i][t][k_2]; 
        }
    }
    
    for (unsigned k_1 = 0; k_1 < this->K; ++k_1)
    {
        double denumerator = 0.0;
        for (unsigned k_3 = 0; k_3 < this->K; ++k_3)
        {
            denumerator += p[k_1][k_3]; 
        }
        for (unsigned k_2 = 0; k_2 < this->K; ++k_2)
        {
            A[k_1][k_2] = p[k_1][k_2] / denumerator;

            if (A[k_1][k_2] <= 0.0) 
                A[k_1][k_2] = DBL_MIN;          // make sure not getting zero
        }
    }
    // keep transProb of '2' -> '3' on min. value
    if (A[2][3] < options.minTransProbCS)
    {
        A[2][3] = options.minTransProbCS;

        if (A[3][3] < options.minTransProbCS)
            A[3][3] = options.minTransProbCS;
        // TODO normalize again ?
        std::cout << "NOTE: Prevented transition probability '2' -> '3' from dropping below min. value of " << options.minTransProbCS << ". Set for transitions '2' -> '3' (and if necessary also for '3'->'3') to " << options.minTransProbCS << "." << std::endl;
    }
    this->transMatrix = A;
}*/

// no scaling, version 2 (does this work?)
/*template<typename TD1, typename TD2>
void HMM<TD1, TD2>::updateTransition_noSc2(String<String<double> > &A, unsigned k_1, unsigned k_2)
{
    double numerator = 0.0;
    for (unsigned t = 1; t < this->T; ++t)
    {
        double sum1 = this->alphas_2[t-1][k_1] * this->transMatrix[k_1][k_2] *  this->eProbs[t][k_2] * betas_2[t][k_2];  
        double sum2 = 0.0;
        for (unsigned k = 0; k <this->K; ++k)
            sum2 += this->alphas_2[t-1][k] * betas_2[t-1][k]; 
        numerator += sum1 / sum2;
    }
    double denumerator = 0.0;
    for (unsigned t = 0; t < this->T - 1; ++t)
        denumerator += this->statePosteriors[k_2][t];
    
    A[k_1][k_2] = numerator / denumerator;  
}
// with scaling, version 2 (does this work ?)
template<typename TD1, typename TD2>
void HMM<TD1, TD2>::updateTransition2(String<String<double> > &A, unsigned k_1, unsigned k_2)
{
    double numerator = 0.0;
    for (unsigned t = 1; t < this->T; ++t)
        numerator += this->alphas_2[t-1][k_1] * this->transMatrix[k_1][k_2] *  this->eProbs[t][k_2] * betas_2[t][k_2];  
   
    double denumerator  = 0.0;
    for (unsigned t = 1; t < this->T; ++t)
    {
        double norm = 0.0;
        for (unsigned k = 0; k < this->K; ++k)      
            norm += this->alphas_1[t-1][k];

        denumerator  += this->alphas_2[t-1][k_1] * this->betas_2[t-1][k_1] * norm;
    }
    A[k_1][k_2] = numerator / denumerator ;  
}*/


template<>
bool HMM<GAMMA2, GAMMA2, ZTBIN, ZTBIN>::updateDensityParams(GAMMA2 &d1, GAMMA2 &d2, AppOptions &options)   
{
    String<String<String<double> > > statePosteriors1;
    String<String<String<double> > > statePosteriors2;
    resize(statePosteriors1, 2, Exact());
    resize(statePosteriors2, 2, Exact());
    for (unsigned s = 0; s < 2; ++s)
    {
        resize(statePosteriors1[s], length(this->statePosteriors[s][0]), Exact());
        resize(statePosteriors2[s], length(this->statePosteriors[s][0]), Exact());
        for (unsigned i = 0; i < length(this->statePosteriors[s][0]); ++i)
        {
            resize(statePosteriors1[s][i], length(this->statePosteriors[s][0][i]), Exact());
            resize(statePosteriors2[s][i], length(this->statePosteriors[s][0][i]), Exact());
            for (unsigned t = 0; t < length(this->statePosteriors[s][0][i]); ++t)
            {
                statePosteriors1[s][i][t] = this->statePosteriors[s][0][i][t] + this->statePosteriors[s][1][i][t];
                statePosteriors2[s][i][t] = this->statePosteriors[s][2][i][t] + this->statePosteriors[s][3][i][t];
            }
        }
    }

    if (options.gslSimplex2)
    {
        if (!d1.updateThetaAndK(statePosteriors1, this->setObs, options.g1_kMin, options.g1_kMax, options))
            return false;

        if (!d2.updateThetaAndK(statePosteriors2, this->setObs, options.g2_kMin, options.g2_kMax, options))         // make sure g1k <= g2k
            return false;

        // make sure gamma1.mu < gamma2.mu   
        checkOrderG1G2(d1, d2, options);
    }
    else    // TODO get rid of this
    {
        // 
        d1.updateTheta(statePosteriors1, this->setObs, options);  
        d2.updateTheta(statePosteriors2, this->setObs, options);

        // make sure gamma1.mu < gamma2.mu    
        checkOrderG1G2(d1, d2, options);

        d1.updateK(statePosteriors1, this->setObs, options.g1_kMin, options.g1_kMax, options);  

        d2.updateK(statePosteriors2, this->setObs, options.g2_kMin, options.g2_kMax, options); 
    }
    return true;
}


template<>
bool HMM<GAMMA2, GAMMA2, ZTBIN_REG, ZTBIN_REG>::updateDensityParams(GAMMA2 &d1, GAMMA2 &d2, AppOptions &options)   
{
    String<String<String<double> > > statePosteriors1;
    String<String<String<double> > > statePosteriors2;
    resize(statePosteriors1, 2, Exact());
    resize(statePosteriors2, 2, Exact());
    for (unsigned s = 0; s < 2; ++s)
    {
        resize(statePosteriors1[s], length(this->statePosteriors[s][0]), Exact());
        resize(statePosteriors2[s], length(this->statePosteriors[s][0]), Exact());
        for (unsigned i = 0; i < length(this->statePosteriors[s][0]); ++i)
        {
            resize(statePosteriors1[s][i], length(this->statePosteriors[s][0][i]), Exact());
            resize(statePosteriors2[s][i], length(this->statePosteriors[s][0][i]), Exact());
            for (unsigned t = 0; t < length(this->statePosteriors[s][0][i]); ++t)
            {
                statePosteriors1[s][i][t] = this->statePosteriors[s][0][i][t] + this->statePosteriors[s][1][i][t];
                statePosteriors2[s][i][t] = this->statePosteriors[s][2][i][t] + this->statePosteriors[s][3][i][t];
            }
        }
    }

    if (options.gslSimplex2)
    {
        if (!d1.updateThetaAndK(statePosteriors1, this->setObs, options.g1_kMin, options.g1_kMax, options))
            return false;

        if (!d2.updateThetaAndK(statePosteriors2, this->setObs, options.g2_kMin, options.g2_kMax, options))
            return false;

        // make sure gamma1.mu < gamma2.mu   
        checkOrderG1G2(d1, d2, options);
    }
    else    // TODO get rid of this
    {
        // 
        d1.updateTheta(statePosteriors1, this->setObs, options);  
        d2.updateTheta(statePosteriors2, this->setObs, options);

        // make sure gamma1.mu < gamma2.mu    
        checkOrderG1G2(d1, d2, options);

        d1.updateK(statePosteriors1, this->setObs, options.g1_kMin, options.g1_kMax, options); 

        d2.updateK(statePosteriors2, this->setObs, options.g2_kMin, options.g2_kMax, options); 
    }
    return true;
}


template<>
bool HMM<GAMMA2_REG, GAMMA2_REG, ZTBIN, ZTBIN>::updateDensityParams(GAMMA2_REG &d1, GAMMA2_REG &d2, AppOptions &options)   
{
    String<String<String<double> > > statePosteriors1;
    String<String<String<double> > > statePosteriors2;
    resize(statePosteriors1, 2, Exact());
    resize(statePosteriors2, 2, Exact());
    for (unsigned s = 0; s < 2; ++s)
    {
        resize(statePosteriors1[s], length(this->statePosteriors[s][0]), Exact());
        resize(statePosteriors2[s], length(this->statePosteriors[s][0]), Exact());
        for (unsigned i = 0; i < length(this->statePosteriors[s][0]); ++i)
        {
            resize(statePosteriors1[s][i], length(this->statePosteriors[s][0][i]), Exact());
            resize(statePosteriors2[s][i], length(this->statePosteriors[s][0][i]), Exact());
            for (unsigned t = 0; t < length(this->statePosteriors[s][0][i]); ++t)
            {
                statePosteriors1[s][i][t] = this->statePosteriors[s][0][i][t] + this->statePosteriors[s][1][i][t];
                statePosteriors2[s][i][t] = this->statePosteriors[s][2][i][t] + this->statePosteriors[s][3][i][t];
            }
        }
    }

    if (options.gslSimplex2)
    {
        if (!d1.updateRegCoeffsAndK(statePosteriors1, this->setObs, options.g1_kMin, options.g1_kMax, options))
            return false;

        double g2_kMin = options.g2_kMin;
        if (options.g1_k_le_g2_k)
            g2_kMin = std::max(d1.k, options.g2_kMin);

        if (!d2.updateRegCoeffsAndK(statePosteriors2, this->setObs, g2_kMin, options.g2_kMax, options))
            return false;

        // make sure gamma1.mu < gamma2.mu    
         checkOrderG1G2(d1, d2, options);
    }
    else
    { 
        d1.updateMean(statePosteriors1, this->setObs, options); 
        d2.updateMean(statePosteriors2, this->setObs, options);

        // make sure gamma1.mu < gamma2.mu    
        checkOrderG1G2(d1, d2, options);

        d1.updateK(statePosteriors1, this->setObs, options.g1_kMin, options.g1_kMax, options); 

        double g2_kMin = options.g2_kMin;
        if (options.g1_k_le_g2_k)
            g2_kMin = std::max(d1.k, options.g2_kMin);

        d2.updateK(statePosteriors2, this->setObs, g2_kMin, options.g2_kMax, options);
    }
    return true;
}

template<>
bool HMM<GAMMA2_REG, GAMMA2_REG, ZTBIN_REG, ZTBIN_REG>::updateDensityParams(GAMMA2_REG &d1, GAMMA2_REG &d2, AppOptions &options)   
{
    String<String<String<double> > > statePosteriors1;
    String<String<String<double> > > statePosteriors2;
    resize(statePosteriors1, 2, Exact());
    resize(statePosteriors2, 2, Exact());
    for (unsigned s = 0; s < 2; ++s)
    {
        resize(statePosteriors1[s], length(this->statePosteriors[s][0]), Exact());
        resize(statePosteriors2[s], length(this->statePosteriors[s][0]), Exact());
        for (unsigned i = 0; i < length(this->statePosteriors[s][0]); ++i)
        {
            resize(statePosteriors1[s][i], length(this->statePosteriors[s][0][i]), Exact());
            resize(statePosteriors2[s][i], length(this->statePosteriors[s][0][i]), Exact());
            for (unsigned t = 0; t < length(this->statePosteriors[s][0][i]); ++t)
            {
                statePosteriors1[s][i][t] = this->statePosteriors[s][0][i][t] + this->statePosteriors[s][1][i][t];
                statePosteriors2[s][i][t] = this->statePosteriors[s][2][i][t] + this->statePosteriors[s][3][i][t];
            }
        }
    }

    if (options.gslSimplex2)
    {
        if (!d1.updateRegCoeffsAndK(statePosteriors1, this->setObs, options.g1_kMin, options.g1_kMax, options))
            return false;

        double g2_kMin = options.g2_kMin;
        if (options.g1_k_le_g2_k)
            g2_kMin = std::max(d1.k, options.g2_kMin);

        if (!d2.updateRegCoeffsAndK(statePosteriors2, this->setObs, g2_kMin, options.g2_kMax, options))
            return false;

        // make sure gamma1.mu < gamma2.mu    
         checkOrderG1G2(d1, d2, options);
    }
    else
    { 
        d1.updateMean(statePosteriors1, this->setObs, options); 
        d2.updateMean(statePosteriors2, this->setObs, options);

        // make sure gamma1.mu < gamma2.mu    
        checkOrderG1G2(d1, d2, options);

        d1.updateK(statePosteriors1, this->setObs, options.g1_kMin, options.g1_kMax, options);  

        double g2_kMin = options.g2_kMin;
        if (options.g1_k_le_g2_k)
            g2_kMin = std::max(d1.k, options.g2_kMin);

        d2.updateK(statePosteriors2, this->setObs, g2_kMin, options.g2_kMax, options);
    }
    return true;
}


template<typename TD1, typename TD2, typename TB1, typename TB2>
bool HMM<TD1, TD2, TB1, TB2>::updateDensityParams(TD1 /*&d1*/, TD2 /*&d2*/, TB1 &bin1, TB2 &bin2, AppOptions &options)   
{
    String<String<String<double> > > statePosteriors1;
    String<String<String<double> > > statePosteriors2;
    resize(statePosteriors1, 2, Exact());
    resize(statePosteriors2, 2, Exact());
    for (unsigned s = 0; s < 2; ++s)
    {
        resize(statePosteriors1[s], length(this->statePosteriors[s][0]), Exact());
        resize(statePosteriors2[s], length(this->statePosteriors[s][0]), Exact());
        for (unsigned i = 0; i < length(this->statePosteriors[s][0]); ++i)
        {
            resize(statePosteriors1[s][i], length(this->statePosteriors[s][0][i]), Exact());
            resize(statePosteriors2[s][i], length(this->statePosteriors[s][0][i]), Exact());
            for (unsigned t = 0; t < length(this->statePosteriors[s][0][i]); ++t)
            {
                statePosteriors1[s][i][t] = this->statePosteriors[s][2][i][t];
                statePosteriors2[s][i][t] = this->statePosteriors[s][3][i][t];
            }
        }
    }

    // truncation counts
    bin1.updateP(statePosteriors1, this->setObs, options); 
    bin2.updateP(statePosteriors2, this->setObs, options);

    // make sure bin1.p < bin2.p   
    checkOrderBin1Bin2(bin1, bin2);

    return true;
}



// Baum-Welch
// E: compute state posterior (gamma), transition posterior (xi)
// M: estimate parameters
/*template<typename TD1, typename TD2, typename TB1, typename TB2> 
bool HMM<TD1, TD2, TB1, TB2>::baumWelch_noSc(TD1 &d1, TD2 &d2, TB1 &bin1, TB2 &bin2, CharString learnTag, AppOptions &options)
{
    double prev_p = 666.0;
    for (unsigned iter = 0; iter < options.maxIter_bw; ++iter)
    {
        std::cout << ".. " << iter << "th iteration " << std::endl;
        if (!computeEmissionProbs(d1, d2, bin1, bin2, options))
        {
            std::cerr << "ERROR: Could not compute emission probabilities! " << std::endl;
            return false;
        }
        // E-step
        forward_noSc();
         //Note: likelihood of all sites, not only selected for parameter fitting, not necessarly increases! 

        backward_noSc();
        computeStatePosteriors();
        // M-step 
        for (unsigned s = 0; s < 2; ++s)
            for (unsigned i = 0; i < length(this->setObs[s]); ++i)
                for (unsigned k = 0; k < this->K; ++k)
                    this->initProbs[s][i][k] = this->statePosteriors[s][k][i][0];   
 
        updateTransition(options);

        if (learnTag == "LEARN_BINOMIAL")
        {
            if (!updateDensityParams(d1, d2, bin1, bin2, options))
            {
                std::cerr << "ERROR: Could not update parameters! " << std::endl;
                return false;
            }
        }
        else
        {
            if (!updateDensityParams(d1, d2, options))
            {
                std::cerr << "ERROR: Could not update parameters! " << std::endl;
                return false;
            }
        }
    }
    return true;
}*/

// with scaling
template<typename TD1, typename TD2, typename TB1, typename TB2> 
bool HMM<TD1, TD2, TB1, TB2>::baumWelch(TD1 &d1, TD2 &d2, TB1 &bin1, TB2 &bin2, CharString learnTag, AppOptions &options)
{
    TD1 prev_d1 = d1;
    TD2 prev_d2 = d2;
    TB1 prev_bin1 = bin1;
    TB2 prev_bin2 = bin2;
    for (unsigned iter = 0; iter < options.maxIter_bw; ++iter)
    {
        std::cout << ".. " << iter << "th iteration " << std::endl;
        std::cout << "                        computeEmissionProbs() " << std::endl;
        if (!computeEmissionProbs(d1, d2, bin1, bin2, options) )
        {
            std::cerr << "ERROR: Could not compute emission probabilities! " << std::endl;
            return false;
        }
        std::cout << "                        computeStatePosteriorsFB() " << std::endl;
        computeStatePosteriorsFBupdateTrans(options);
        
        std::cout << "                        updateDensityParams() " << std::endl;

        if (learnTag == "LEARN_BINOMIAL")
        {
            if (!updateDensityParams(d1, d2, bin1, bin2, options))
            {
                std::cerr << "ERROR: Could not update parameters! " << std::endl;
                return false;
            }
        }
        else
        {
            if (!updateDensityParams(d1, d2, options))
            {
                std::cerr << "ERROR: Could not update parameters! " << std::endl;
                return false;
            }
        }
        
        if (learnTag == "LEARN_GAMMA" && checkConvergence(d1, prev_d1, options) && checkConvergence(d2, prev_d2, options) )             
        {
            std::cout << " **** Convergence ! **** " << std::endl;
            break;
        }
        else if (learnTag != "LEARN_GAMMA" && checkConvergence(bin1, prev_bin1, options) && checkConvergence(bin2, prev_bin2, options) )             
        {
            std::cout << " **** Convergence ! **** " << std::endl;
            break;
        }
        prev_d1 = d1;
        prev_d2 = d2;
        prev_bin1 = bin1;
        prev_bin2 = bin2;

        myPrint(d1);
        myPrint(d2);
        
        std::cout << "*** Transition probabilitites ***" << std::endl;
        for (unsigned k_1 = 0; k_1 < this->K; ++k_1)
        {
            std::cout << "    " << k_1 << ": ";
            for (unsigned k_2 = 0; k_2 < this->K; ++k_2)
                std::cout << this->transMatrix[k_1][k_2] << "  ";
            std::cout << std::endl;
        }
        std::cout << std::endl;
        if (learnTag != "LEARN_GAMMA")
         {
            myPrint(bin1);
            myPrint(bin2);
        }
    }
    return true;
}


template<typename TD1, typename TD2, typename TB1, typename TB2> 
bool HMM<TD1, TD2, TB1, TB2>::applyParameters(TD1 &d1, TD2 &d2, TB1 &bin1, TB2 &bin2, AppOptions &options)
{
    if (!computeEmissionProbs(d1, d2, bin1, bin2, options))
    {
        std::cerr << "ERROR: Could not compute emission probabilities! " << std::endl;
        return false;
    }
    computeStatePosteriorsFB(options);

    return true;
}


// returns log P
template<typename TD1, typename TD2, typename TB1, typename TB2>
double HMM<TD1, TD2, TB1, TB2>::viterbi(String<String<String<__uint8> > > &states)
{
    double p = 1.0;
    for (unsigned s = 0; s < 2; ++s)
    {
        resize(states[s], length(this->setObs[s]), Exact());
        for (unsigned i = 0; i < length(this->setObs[s]); ++i)
        {
            resize(states[s][i], this->setObs[s][i].length(), Exact());
            // store for each t and state maximizing precursor joint probability of state sequence and observation
            String<String<double> > vits;
            resize(vits, this->setObs[s][i].length(), Exact());
            for (unsigned t = 0; t < this->setObs[s][i].length(); ++t)
                resize(vits[t], this->K, Exact());
            // store for each t and state maximizing precursor state
            String<String<unsigned> > track;
            resize(track, this->setObs[s][i].length(), Exact());
            for (unsigned t = 0; t < this->setObs[s][i].length(); ++t)
                resize(track[t], this->K, Exact());

            // initialize
            for (unsigned k = 0; k < this->K; ++k)
                vits[0][k] = this->initProbs[s][i][k] * this->eProbs[s][i][0][k];
            // recursion
            for (unsigned t = 1; t < this->setObs[s][i].length(); ++t)
            {
                for (unsigned k = 0; k < this->K; ++k)
                {
                    double max_v = vits[t-1][0] * this->transMatrix[0][k];
                    unsigned max_k = 0;
                    for (unsigned k_p = 1; k_p < this->K; ++k_p)
                    {
                        double v = vits[t-1][k_p] * this->transMatrix[k_p][k];
                        if (v > max_v)
                        {
                            max_v = v;
                            max_k = k_p;
                        }
                    }
                    vits[t][k] = max_v * this->eProbs[s][i][t][k];
                    track[t][k] = max_k;
                }
            }
            // backtracking
            double max_v = vits[this->setObs[s][i].length() - 1][0];
            unsigned max_k = 0;
            for (unsigned k = 1; k < this->K; ++k)
            {
                if (vits[this->setObs[s][i].length() - 1][k] >= max_v)
                {
                    max_v = vits[this->setObs[s][i].length() - 1][k];
                    max_k = k;
                }
            }
            states[s][i][this->setObs[s][i].length() - 1] = max_k;
            for (int t = this->setObs[s][i].length() - 2; t >= 0; --t)
                states[s][i][t] = track[t+1][states[s][t+1]];
            
            p *= max_v;
        }
    }
    return p;   
}

template<typename TD1, typename TD2, typename TB1, typename TB2>
double HMM<TD1, TD2, TB1, TB2>::viterbi_log(String<String<String<__uint8> > > &states)
{
    double p = 0.0;
    for (unsigned s = 0; s < 2; ++s)
    {
        resize(states[s], length(this->setObs[s]), Exact());
        for (unsigned i = 0; i < length(this->setObs[s]); ++i)
        {
            resize(states[s][i], this->setObs[s][i].length(), Exact());
            // store for each t and state maximizing precursor joint probability of state sequence and observation
            String<String<double> > vits;
            resize(vits, this->setObs[s][i].length(), Exact());
            for (unsigned t = 0; t < this->setObs[s][i].length(); ++t)
                resize(vits[t], this->K, Exact());
            // store for each t and state maximizing precursor state
            String<String<unsigned> > track;
            resize(track, this->setObs[s][i].length(), Exact());
            for (unsigned t = 0; t < this->setObs[s][i].length(); ++t)
                resize(track[t], this->K, Exact());

            // SEQAN_ASSERT_GT( ,0.0) or <- DBL_MIN

            // initialize
            for (unsigned k = 0; k < this->K; ++k)
                vits[0][k] = log(this->initProbs[s][i][k]) + log(this->eProbs[s][i][0][k]);
            // recursion
            for (unsigned t = 1; t < this->setObs[s][i].length(); ++t)
            {
                for (unsigned k = 0; k < this->K; ++k)
                {
                    double max_v = vits[t-1][0] + log(this->transMatrix[0][k]);
                    unsigned max_k = 0;
                    for (unsigned k_p = 1; k_p < this->K; ++k_p)
                    {
                        double v = vits[t-1][k_p] + log(this->transMatrix[k_p][k]);
                        if (v > max_v)
                        {
                            max_v = v;
                            max_k = k_p;
                        }
                    }
                    vits[t][k] = max_v + log(this->eProbs[s][i][t][k]);
                    track[t][k] = max_k;
                }
            }
            // backtracking
            double max_v = vits[this->setObs[s][i].length() - 1][0];
            unsigned max_k = 0;
            for (unsigned k = 1; k < this->K; ++k)
            {
                if (vits[this->setObs[s][i].length() - 1][k] >= max_v)
                {
                    max_v = vits[this->setObs[s][i].length() - 1][k];
                    max_k = k;
                }
            }
            states[s][i][this->setObs[s][i].length() - 1] = max_k;
            for (int t = this->setObs[s][i].length() - 2; t >= 0; --t)
                states[s][i][t] = track[t+1][states[s][i][t+1]];

            p += max_v;
        }
    }
    // NOTE p: of all sites, not only selected for parameter fitting, not necessarly increases!
    return p;        
}


template<typename TD1, typename TD2, typename TB1, typename TB2>
void HMM<TD1, TD2, TB1, TB2>::posteriorDecoding(String<String<String<__uint8> > > &states)
{ 
    for (unsigned s = 0; s < 2; ++s)
    {
        resize(states[s], length(this->setObs[s]), Exact());
        for (unsigned i = 0; i < length(this->setObs[s]); ++i)
        {
            resize(states[s][i], this->setObs[s][i].length(), Exact());
            for (unsigned t = 0; t < this->setObs[s][i].length(); ++t)
            {
                double max_p = 0.0;
                unsigned max_k = 0;
                for (unsigned k = 0; k < this->K; ++k)
                {
                    if (this->statePosteriors[s][k][i][t] > max_p)
                    {
                        max_p = this->statePosteriors[s][k][i][t];
                        max_k = k;
                    }
                }
                states[s][i][t] = max_k;
            }
        }
    }
}



void writeStates(BedFileOut &outBed,
                 Data &data,
                 FragmentStore<> &store, 
                 unsigned contigId,
                 AppOptions &options)          
{  
    for (unsigned s = 0; s < 2; ++s)
    {
        for (unsigned i = 0; i < length(data.states[s]); ++i)
        {
            for (unsigned t = 0; t < length(data.states[s][i]); ++t)
            {
                if (options.outputAll && data.setObs[s][i].truncCounts[t] >= 1)
                {
                    BedRecord<Bed6> record;

                    record.ref = store.contigNameStore[contigId];
                    if (s == 0)         // crosslink sites (not truncation site)
                    {
                        record.beginPos = t + data.setPos[s][i] - 1;
                        record.endPos = record.beginPos + 1;
                    }
                    else
                    {
                        record.beginPos = length(store.contigStore[contigId].seq) - (t + data.setPos[s][i]) ;
                        record.endPos = record.beginPos + 1;
                    }

                    std::stringstream ss;
                    ss << (int)data.states[s][i][t];
                    record.name = ss.str();
                    ss.str("");  
                    ss.clear();  

                    // log posterior prob. ratio score
                    double secondBest = 0.0;
                    for (unsigned k = 0; k < 4; ++k)
                    {
                        if (k != (unsigned)data.states[s][i][t] && data.statePosteriors[s][k][i][t] > secondBest)
                            secondBest = data.statePosteriors[s][k][i][t];
                    }                    
                    ss << (double)log(data.statePosteriors[s][data.states[s][i][t]][i][t] / std::max(secondBest, DBL_MIN) );

                    record.score = ss.str();
                    ss.str("");  
                    ss.clear();  
                    if (s == 0)
                        record.strand = '+';
                    else
                        record.strand = '-';

                    ss << 0;
                    ss << ";";
                    ss << (int)data.setObs[s][i].truncCounts[t];
                    ss << ";";
                    ss << (int)data.setObs[s][i].nEstimates[t];
                    ss << ";";
                    ss << (double)data.setObs[s][i].kdes[t];
                    ss << ";";

                    ss << (double)data.statePosteriors[s][3][i][t];
                    ss << ";"; 
                    if (options.useCov_RPKM)
                        ss << (double)data.setObs[s][i].rpkms[t];
                    else
                        ss << 0.0;
                    ss << ";";
                    ss << (double)log((data.statePosteriors[s][2][i][t] + data.statePosteriors[s][3][i][t])/(data.statePosteriors[s][0][i][t] + data.statePosteriors[s][1][i][t]));
                    ss << ";";

                    record.data = ss.str();
                    ss.str("");  
                    ss.clear();  

                    writeRecord(outBed, record);
                }
                else if (data.states[s][i][t] == 3)
                {
                    BedRecord<Bed6> record;

                    record.ref = store.contigNameStore[contigId];
                    if (s == 0)         // crosslink sites (not truncation site)
                    {
                        record.beginPos = t + data.setPos[s][i] - 1;
                        record.endPos = record.beginPos + 1;
                    }
                    else
                    {
                        record.beginPos = length(store.contigStore[contigId].seq) - (t + data.setPos[s][i]);
                        record.endPos = record.beginPos + 1;
                    }

                    std::stringstream ss;
                    ss << (int)data.states[s][i][t];
                    record.name = ss.str();
                    ss.str("");  
                    ss.clear();  

                    // log posterior prob. ratio score
                    double secondBest = 0.0;
                    for (unsigned k = 0; k < 4; ++k)
                    {
                        if (k != (unsigned)data.states[s][i][t] && data.statePosteriors[s][k][i][t] > secondBest)
                            secondBest = data.statePosteriors[s][k][i][t];
                    }                    
                    ss << (double)log(data.statePosteriors[s][data.states[s][i][t]][i][t] / std::max(secondBest, DBL_MIN) );

                    record.score = ss.str();
                    ss.str("");  
                    ss.clear();  
                    if (s == 0)
                        record.strand = '+';
                    else
                        record.strand = '-';

                    writeRecord(outBed, record);
                }               
            }
        }
    }
}


void writeRegions(BedFileOut &outBed,
                 Data &data,
                 FragmentStore<> &store, 
                 unsigned contigId,
                 AppOptions &options)          
{  
    for (unsigned s = 0; s < 2; ++s)
    {
        for (unsigned i = 0; i < length(data.states[s]); ++i)
        {
            for (unsigned t = 0; t < length(data.states[s][i]); ++t)
            {
                if (data.states[s][i][t] == 3)
                {
                    BedRecord<Bed6> record;
                    record.ref = store.contigNameStore[contigId];
                    if (s == 0)         // crosslink sites (not truncation site)
                    {
                        record.beginPos = t + data.setPos[s][i] - 1;
                        record.endPos = record.beginPos + 1;
                    }
                    else
                    {
                        record.beginPos = length(store.contigStore[contigId].seq) - (t + data.setPos[s][i]);
                        record.endPos = record.beginPos + 1;
                    }

                    std::stringstream ss;

                    // log posterior prob. ratio score
                    double secondBest = 0.0;
                    for (unsigned k = 0; k < 4; ++k)
                    {
                        if (k != (unsigned)data.states[s][i][t] && data.statePosteriors[s][k][i][t] > secondBest)
                            secondBest = data.statePosteriors[s][k][i][t];
                    }                    
                    ss << (double)log(data.statePosteriors[s][data.states[s][i][t]][i][t] / std::max(secondBest, DBL_MIN) );
                    record.score = ss.str();
                    ss.str("");  
                    ss.clear();  
                    if (s == 0)
                        record.strand = '+';
                    else
                        record.strand = '-';

                    unsigned prev_cs = t;
                    double scoresSum = (double)log(data.statePosteriors[s][data.states[s][i][t]][i][t] / std::max(secondBest, DBL_MIN) );
                    std::stringstream ss_indivScores;
                    ss_indivScores << (double)log(data.statePosteriors[s][data.states[s][i][t]][i][t] / std::max(secondBest, DBL_MIN) ) << ';';
                    while ((t+1) < length(data.states[s][i]) && (t+1-prev_cs) <= options.distMerge)
                    {
                        ++t;
                        if (data.states[s][i][t] == 3)
                        {
                            if (s == 0)         // crosslink sites (not truncation site)
                            {
                                record.endPos = t + data.setPos[s][i] - 1;
                            }
                            else
                            {
                                record.beginPos = length(store.contigStore[contigId].seq) - (t + data.setPos[s][i]);
                            }

                            // log posterior prob. ratio score
                            double secondBest = 0.0;
                            for (unsigned k = 0; k < 4; ++k)
                            {
                                if (k != (unsigned)data.states[s][i][t] && data.statePosteriors[s][k][i][t] > secondBest)
                                    secondBest = data.statePosteriors[s][k][i][t];
                            }                   

                            scoresSum += (double)log(data.statePosteriors[s][data.states[s][i][t]][i][t] / std::max(secondBest, DBL_MIN) );
                            ss_indivScores << (double)log(data.statePosteriors[s][data.states[s][i][t]][i][t] / std::max(secondBest, DBL_MIN) ) << ';';
                            prev_cs = t;
                        }
                    }
                    ss << scoresSum;
                    record.score = ss.str();
                    ss.str("");  
                    ss.clear(); 
                    record.name = ss_indivScores.str();
                    ss_indivScores.str("");  
                    ss_indivScores.clear(); 
                    writeRecord(outBed, record);
                }      
            }
        }
    }
}

  
template<typename TD1, typename TD2, typename TB1, typename TB2>
void myPrint(HMM<TD1, TD2, TB1, TB2> &hmm)
{
    std::cout << "*** Transition probabilitites ***" << std::endl;
    for (unsigned k_1 = 0; k_1 < hmm.K; ++k_1)
    {
        std::cout << "    " << k_1 << ": ";
        for (unsigned k_2 = 0; k_2 < hmm.K; ++k_2)
            std::cout << hmm.transMatrix[k_1][k_2] << "  ";
        std::cout << std::endl;
    }
}

#endif
