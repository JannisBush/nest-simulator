/*
 *  iaf_wang_2002.cpp
 *
 *  This file is part of NEST.
 *
 *  Copyright (C) 2004 The NEST Initiative
 *
 *  NEST is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  NEST is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with NEST.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "iaf_wang_2002.h"

#ifdef HAVE_GSL

// Includes from libnestutil:
#include "dictdatum.h"
#include "dict_util.h"
#include "numerics.h"

// Includes from nestkernel:
#include "exceptions.h"
#include "kernel_manager.h"
#include "universal_data_logger_impl.h"

// Includes from sli:
#include "dict.h"
#include "dictutils.h"
#include "doubledatum.h"
#include "integerdatum.h"
#include "lockptrdatum.h"

/* ---------------------------------------------------------------------------
 * Recordables map
 * --------------------------------------------------------------------------- */
nest::RecordablesMap< nest::iaf_wang_2002 > nest::iaf_wang_2002::recordablesMap_;

namespace nest
{
/*
 * Override the create() method with one call to RecordablesMap::insert_()
 * for each quantity to be recorded.
 */
template <>
void
RecordablesMap< iaf_wang_2002 >::create()
{
  // add state variables to recordables map
  insert_( names::V_m, &iaf_wang_2002::get_ode_state_elem_< iaf_wang_2002::State_::V_m > );
  insert_( names::s_AMPA, &iaf_wang_2002::get_ode_state_elem_< iaf_wang_2002::State_::s_AMPA > );
  insert_( names::s_GABA, &iaf_wang_2002::get_ode_state_elem_< iaf_wang_2002::State_::s_GABA > );
  insert_( names::s_NMDA, &iaf_wang_2002::get_ode_state_elem_< iaf_wang_2002::State_::s_NMDA > );
}
}

extern "C" inline int
nest::iaf_wang_2002_dynamics( double, const double y[], double f[], void* pnode )
{
  // a shorthand
  typedef nest::iaf_wang_2002::State_ S;

  // get access to node so we can almost work as in a member function
  assert( pnode );
  const nest::iaf_wang_2002& node = *( reinterpret_cast< nest::iaf_wang_2002* >( pnode ) );

  // y[] here is---and must be---the state vector supplied by the integrator,
  // not the state vector in the node, node.S_.y[].

  const double I_AMPA = ( y[ S::V_m ] - node.P_.E_ex ) * y[ S::s_AMPA ];

  const double I_rec_GABA = ( y[ S::V_m ] - node.P_.E_in ) * y[ S::s_GABA ];

  const double I_rec_NMDA = ( y[ S::V_m ] - node.P_.E_ex )
    / ( 1 + node.P_.conc_Mg2 * std::exp( -0.062 * y[ S::V_m ] ) / 3.57 ) * y[ S::s_NMDA ]; 

  const double I_syn = I_AMPA + I_rec_GABA + I_rec_NMDA - node.B_.I_stim_;

  f[ S::V_m ] = ( -node.P_.g_L * ( y[ S::V_m ] - node.P_.E_L ) - I_syn ) / node.P_.C_m;

  f[ S::s_AMPA ] = -y[ S::s_AMPA ] / node.P_.tau_AMPA;
  f[ S::s_NMDA ] = -y[ S::s_NMDA ] / node.P_.tau_decay_NMDA;
  f[ S::s_GABA ] = -y[ S::s_GABA ] / node.P_.tau_GABA;

  return GSL_SUCCESS;
}



/* ---------------------------------------------------------------------------
 * Default constructors defining default parameters and state
 * --------------------------------------------------------------------------- */

nest::iaf_wang_2002::Parameters_::Parameters_()
  : E_L( -70.0 )          // mV
  , E_ex( 0.0 )           // mV
  , E_in( -70.0 )         // mV
  , V_th( -55.0 )         // mV
  , V_reset( -60.0 )      // mV
  , C_m( 500.0 )          // pF
  , g_L( 25.0 )           // nS
  , t_ref( 2.0 )          // ms
  , tau_AMPA( 2.0 )       // ms
  , tau_GABA( 5.0 )       // ms
  , tau_decay_NMDA( 100 ) // ms
  , alpha( 0.5 )          // 1 / ms
  , conc_Mg2( 1 )         // mM
  , gsl_error_tol( 1e-3 )
{
}

nest::iaf_wang_2002::State_::State_( const Parameters_& p )
  : r_( 0 )
{
  y_[ V_m ] = p.E_L; // initialize to reversal potential
  y_[ s_AMPA ] = 0.0;
  y_[ s_GABA ] = 0.0;
  y_[ s_NMDA ] = 0.0;
}

nest::iaf_wang_2002::State_::State_( const State_& s )
  : r_( s.r_ )
{
  y_[ V_m ] = s.y_[ V_m ];
  y_[ s_AMPA ] = s.y_[ s_AMPA ];
  y_[ s_GABA ] = s.y_[ s_GABA ];
  y_[ s_NMDA ] = s.y_[ s_NMDA ];
}

nest::iaf_wang_2002::Buffers_::Buffers_( iaf_wang_2002& n )
  : logger_( n )
  , spike_AMPA()
  , spike_GABA()
  , spike_NMDA()
  , s_( nullptr )
  , c_( nullptr )
  , e_( nullptr )
  , step_( Time::get_resolution().get_ms() )
  , integration_step_( step_ )
{
  // Initialization of the remaining members is deferred to init_buffers_().
}

nest::iaf_wang_2002::Buffers_::Buffers_( const Buffers_&, iaf_wang_2002& n )
  : logger_( n )
  , s_( nullptr )
  , c_( nullptr )
  , e_( nullptr )
{
  // Initialization of the remaining members is deferred to init_buffers_().
}

/* ---------------------------------------------------------------------------
 * Parameter and state extractions and manipulation functions
 * --------------------------------------------------------------------------- */

void
nest::iaf_wang_2002::Parameters_::get( DictionaryDatum& d ) const
{
  def< double >( d, names::E_L, E_L );
  def< double >( d, names::E_ex, E_ex );
  def< double >( d, names::E_in, E_in );
  def< double >( d, names::V_th, V_th );
  def< double >( d, names::V_reset, V_reset );
  def< double >( d, names::C_m, C_m );
  def< double >( d, names::g_L, g_L );
  def< double >( d, names::t_ref, t_ref );
  def< double >( d, names::tau_AMPA, tau_AMPA );
  def< double >( d, names::tau_GABA, tau_GABA );
  def< double >( d, names::tau_rise_NMDA, tau_rise_NMDA );
  def< double >( d, names::tau_decay_NMDA, tau_decay_NMDA );
  def< double >( d, names::alpha, alpha );
  def< double >( d, names::conc_Mg2, conc_Mg2 );
  def< double >( d, names::gsl_error_tol, gsl_error_tol );
}

void
nest::iaf_wang_2002::Parameters_::set( const DictionaryDatum& d, Node* node )
{
  // allow setting the membrane potential
  updateValueParam< double >( d, names::V_th, V_th, node );
  updateValueParam< double >( d, names::V_reset, V_reset, node );
  updateValueParam< double >( d, names::t_ref, t_ref, node );
  updateValueParam< double >( d, names::E_L, E_L, node );

  updateValueParam< double >( d, names::E_ex, E_ex, node );
  updateValueParam< double >( d, names::E_in, E_in, node );

  updateValueParam< double >( d, names::C_m, C_m, node );
  updateValueParam< double >( d, names::g_L, g_L, node );

  updateValueParam< double >( d, names::tau_AMPA, tau_AMPA, node );
  updateValueParam< double >( d, names::tau_GABA, tau_GABA, node );
  updateValueParam< double >( d, names::tau_rise_NMDA, tau_rise_NMDA, node );
  updateValueParam< double >( d, names::tau_decay_NMDA, tau_decay_NMDA, node );

  updateValueParam< double >( d, names::alpha, alpha, node );
  updateValueParam< double >( d, names::conc_Mg2, conc_Mg2, node );

  updateValueParam< double >( d, names::gsl_error_tol, gsl_error_tol, node );

  if ( V_reset >= V_th )
  {
    throw BadProperty( "Reset potential must be smaller than threshold." );
  }
  if ( C_m <= 0 )
  {
    throw BadProperty( "Capacitance must be strictly positive." );
  }
  if ( t_ref < 0 )
  {
    throw BadProperty( "Refractory time cannot be negative." );
  }
  if ( tau_AMPA <= 0 or tau_GABA <= 0 or tau_rise_NMDA <= 0 or tau_decay_NMDA <= 0 )
  {
    throw BadProperty( "All time constants must be strictly positive." );
  }
  if ( alpha <= 0 )
  {
    throw BadProperty( "alpha > 0 required." );
  }
  if ( conc_Mg2 <= 0 )
  {
    throw BadProperty( "Mg2 concentration must be strictly positive." );
  }
  if ( gsl_error_tol <= 0.0 )
  {
    throw BadProperty( "The gsl_error_tol must be strictly positive." );
  }
}

void
nest::iaf_wang_2002::State_::get( DictionaryDatum& d ) const
{
  def< double >( d, names::V_m, y_[ V_m ] ); // Membrane potential
  def< double >( d, names::s_AMPA, y_[ s_AMPA ] );
  def< double >( d, names::s_GABA, y_[ s_GABA ] );
  def< double >( d, names::s_NMDA, y_[ s_NMDA] );

  // total NMDA sum
  double NMDA_sum = get_NMDA_sum();
  def< double >( d, names::NMDA_sum, NMDA_sum );
}

void
nest::iaf_wang_2002::State_::set( const DictionaryDatum& d, const Parameters_&, Node* node )
{
  updateValueParam< double >( d, names::V_m, y_[ V_m ], node );
  updateValueParam< double >( d, names::s_AMPA, y_[ s_AMPA ], node );
  updateValueParam< double >( d, names::s_GABA, y_[ s_GABA ], node );
  updateValueParam< double >( d, names::s_NMDA, y_[ s_NMDA ], node );
}

/* ---------------------------------------------------------------------------
 * Default constructor for node
 * --------------------------------------------------------------------------- */

nest::iaf_wang_2002::iaf_wang_2002()
  : ArchivingNode()
  , P_()
  , S_( P_ )
  , B_( *this )
{
  recordablesMap_.create();

  calibrate();
}

/* ---------------------------------------------------------------------------
 * Copy constructor for node
 * --------------------------------------------------------------------------- */

nest::iaf_wang_2002::iaf_wang_2002( const iaf_wang_2002& n_ )
  : ArchivingNode( n_ )
  , P_( n_.P_ )
  , S_( n_.S_ )
  , B_( n_.B_, *this )
{
}

/* ---------------------------------------------------------------------------
 * Destructor for node
 * --------------------------------------------------------------------------- */

nest::iaf_wang_2002::~iaf_wang_2002()
{
  // GSL structs may not have been allocated, so we need to protect destruction

  if ( B_.s_ )
  {
    gsl_odeiv_step_free( B_.s_ );
  }

  if ( B_.c_ )
  {
    gsl_odeiv_control_free( B_.c_ );
  }

  if ( B_.e_ )
  {
    gsl_odeiv_evolve_free( B_.e_ );
  }
}

/* ---------------------------------------------------------------------------
 * Node initialization functions
 * --------------------------------------------------------------------------- */

void
nest::iaf_wang_2002::init_state_()
{
}

void
nest::iaf_wang_2002::init_buffers_()
{
  B_.spike_AMPA.clear();
  B_.spike_GABA.clear();
  B_.spike_NMDA.clear();
  B_.currents_.clear(); // includes resize

  B_.logger_.reset(); // includes resize
  ArchivingNode::clear_history();

  if ( B_.s_ == nullptr )
  {
    B_.s_ = gsl_odeiv_step_alloc( gsl_odeiv_step_rkf45, State_::STATE_VEC_SIZE );
  }
  else
  {
    gsl_odeiv_step_reset( B_.s_ );
  }

  if ( B_.c_ == nullptr )
  {
    B_.c_ = gsl_odeiv_control_y_new( P_.gsl_error_tol, 0.0 );
  }
  else
  {
    gsl_odeiv_control_init( B_.c_, P_.gsl_error_tol, 0.0, 1.0, 0.0 );
  }

  if ( B_.e_ == nullptr )
  {
    B_.e_ = gsl_odeiv_evolve_alloc( State_::STATE_VEC_SIZE );
  }
  else
  {
    gsl_odeiv_evolve_reset( B_.e_ );
  }

  B_.sys_.function = iaf_wang_2002_dynamics;
  B_.sys_.jacobian = nullptr;
  B_.sys_.dimension = State_::STATE_VEC_SIZE;
  B_.sys_.params = reinterpret_cast< void* >( this );
  B_.step_ = Time::get_resolution().get_ms();
  B_.integration_step_ = Time::get_resolution().get_ms();

  B_.I_stim_ = 0.0;
}

void
nest::iaf_wang_2002::pre_run_hook()
{
  // ensures initialization in case mm connected after Simulate
  B_.logger_.init();

  V_.RefractoryCounts_ = Time( Time::ms( P_.t_ref ) ).get_steps();
  // since t_ref_ >= 0, this can only fail in error
  assert( V_.RefractoryCounts_ >= 0 );
}

void
nest::iaf_wang_2002::calibrate()
{
  B_.logger_.init();

  // internals V_
  V_.RefractoryCounts_ = Time( Time::ms( ( double ) ( P_.t_ref ) ) ).get_steps();
}

/* ---------------------------------------------------------------------------
 * Update and spike handling functions
 * --------------------------------------------------------------------------- */

void
nest::iaf_wang_2002::update( Time const& origin, const long from, const long to )
{
  std::vector< double > s_vals( kernel().connection_manager.get_min_delay(), 0.0 );

  for ( long lag = from; lag < to; ++lag )
  {
    double t = 0.0;

    // numerical integration with adaptive step size control:
    // ------------------------------------------------------
    // gsl_odeiv_evolve_apply performs only a single numerical
    // integration step, starting from t and bounded by step;
    // the while-loop ensures integration over the whole simulation
    // step (0, step] if more than one integration step is needed due
    // to a small integration step size;
    // note that (t+IntegrationStep > step) leads to integration over
    // (t, step] and afterwards setting t to step, but it does not
    // enforce setting IntegrationStep to step-t; this is of advantage
    // for a consistent and efficient integration across subsequent
    // simulation intervals

    while ( t < B_.step_ )
    {
      const int status = gsl_odeiv_evolve_apply( B_.e_,
        B_.c_,
        B_.s_,
        &B_.sys_,              // system of ODE
        &t,                    // from t
        B_.step_,              // to t <= step
        &B_.integration_step_, // integration step size
        S_.y_ );       // neuronal state

      if ( status != GSL_SUCCESS )
      {
        throw GSLSolverFailure( get_name(), status );
      }
    }


    // add incoming spikes
    S_.y_[ State_::s_AMPA ] += B_.spike_AMPA.get_value( lag );
    S_.y_[ State_::s_GABA ] += B_.spike_GABA.get_value( lag );
    S_.y_[ State_::s_NMDA ] += B_.spike_NMDA.get_value( lag );
    if ( S_.y_[ State_::s_NMDA ] > 1 )
    {
      S_.y_[ State_::s_NMDA ] = 1;
    }
    if ( S_.r_ )
    {
      // neuron is absolute refractory
      --S_.r_;
      S_.y_[ State_::V_m ] = P_.V_reset; // clamp potential
    }
    else if ( S_.y_[ State_::V_m ] >= P_.V_th )
    {
      // neuron is not absolute refractory
      S_.r_ = V_.RefractoryCounts_;
      S_.y_[ State_::V_m ] = P_.V_reset;

      // get previous spike time
      double t_lastspike = get_spiketime_ms();               

      // log spike with ArchivingNode
      set_spiketime( Time::step( origin.get_steps() + lag + 1 ) );

      double t_spike = get_spiketime_ms();

      // compute current value of s_NMDA and add NMDA update to spike offset
      S_.s_NMDA_pre = S_.s_NMDA_pre * exp( -( t_spike - t_lastspike ) / P_.tau_decay_NMDA );
      double s_NMDA_delta = P_.alpha * (1 - S_.s_NMDA_pre);
      S_.s_NMDA_pre += s_NMDA_delta;

      SpikeEvent se;
      se.set_offset( s_NMDA_delta );
      kernel().event_delivery_manager.send( *this, se, lag );
    }

    // set new input current
    B_.I_stim_ = B_.currents_.get_value( lag );

    // voltage logging
    B_.logger_.record_data( origin.get_steps() + lag );
  }
}

// Do not move this function as inline to h-file. It depends on
// universal_data_logger_impl.h being included here.
void
nest::iaf_wang_2002::handle( DataLoggingRequest& e )
{
  B_.logger_.handle( e );
}

void
nest::iaf_wang_2002::handle( SpikeEvent& e )
{
  assert( e.get_delay_steps() > 0 );

  if ( e.get_weight() > 0.0 )
  {
    B_.spike_AMPA.add_value( e.get_rel_delivery_steps( kernel().simulation_manager.get_slice_origin() ),
      e.get_weight() * e.get_multiplicity() );
    
    if ( e.get_rport() == 0 ) {
        B_.spike_NMDA.add_value( e.get_rel_delivery_steps( kernel().simulation_manager.get_slice_origin() ),
      e.get_weight() * e.get_multiplicity() * e.get_offset() );
    }
  }
  else
  {
    B_.spike_GABA.add_value( e.get_rel_delivery_steps( kernel().simulation_manager.get_slice_origin() ),
      -e.get_weight() * e.get_multiplicity() );
  }
}

void
nest::iaf_wang_2002::handle( CurrentEvent& e )
{
  assert( e.get_delay_steps() > 0 );

  B_.currents_.add_value(
    e.get_rel_delivery_steps( kernel().simulation_manager.get_slice_origin() ), e.get_weight() * e.get_current() );
}

#endif // HAVE_GSL
