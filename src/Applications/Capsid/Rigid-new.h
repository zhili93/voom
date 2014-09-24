#include <string>
#include <iostream>
#include <vector>
#include <map>
#include <tvmet/Vector.h>
#include <fstream>
#include "Node.h"
#include "Element.h"

#ifdef _R
#undef _R
#endif

namespace voom {

  class RigidSurface : public Element
  {
  public: 
    virtual double FZ() const = 0;
    virtual double penetration() const {return _penetration;}
  protected:
    double _penetration;
  };


  class RigidHemisphereAL : public RigidSurface
  {
  public:

    // typedefs
    typedef tvmet::Vector<double,3> Vector3D;
    typedef DeformationNode<3> DefNode;
    typedef std::vector< DefNode* > DefNodeContainer;
    typedef DefNodeContainer::iterator DefNodeIterator;
    typedef DefNodeContainer::const_iterator ConstDefNodeIterator;

    RigidHemisphereAL( const DefNodeContainer & nodes, 
		       double k, double R, Vector3D xc, double friction=0.0 ) {
      _defNodes = nodes;
      for(ConstDefNodeIterator n=_defNodes.begin(); n!=_defNodes.end(); n++) 
	_baseNodes.push_back(*n);

      _k = k;
      _R = R;
      _xc = xc;
      _mu = friction;
      _FZ = 0.0;
      _penetration = 0.0;
      _active.resize(nodes.size());
      _forces.resize(_defNodes.size());
      _forces = Vector3D(0.0);
      _pressureMultipliers.resize(_defNodes.size());
      _pressureMultipliers = 0.0;
      _frictionMultipliers.resize(_defNodes.size());
      _frictionMultipliers = Vector3D(0.0);
      _normals.resize(_defNodes.size());
      _normals = Vector3D(0.0);
      updateContact();

    }

    double penaltyCoefficient() const {return _k;}

    void setPenaltyCoefficient( double k ) { _k = k; }

    int active() const { 
      int n=0; 
      for(int i=0; i<_active.size(); i++) {
	if (_active[i]) n++;
      }
      return n; 
    }

    void updateContact() {

      // Now update surface normals and check for active contacts
      for(int a=0; a<_defNodes.size(); a++) {
	const Vector3D & x = _defNodes[a]->point();
	double R = tvmet::norm2(x-_xc);

	// recompute outward pointing normal vector 
	Vector3D & n = _normals(a);
	n = x - _xc;
	n /=  R;
	
	// check for active contacts
 	if( R <= _R ) {  // we are in contact (penetrating)	
	  _active[a] = true;
	} else {
	  _active[a] = false;
	}

	// Uzawa update: set multipliers equal to most recently computed
	// forces
	_pressureMultipliers(a) = dot(_forces(a), _normals(a));
	_frictionMultipliers(a) = _forces(a) 
	  			- _pressureMultipliers(a)*_normals(a);
	

      }


      return;
    }

    double FZ() const {return _FZ;}

    double penetration() const {return _penetration;}

    // E = 0.25 * k * ( (x-xc)^2 - R^2 )^2
    // f = dE/dx = k * ( (x-xc)^2 - R^2 )*(x-xc)
    //! Compute contact energy and forces
    virtual void compute( bool f0, bool f1, bool f2 ) {
      //std::cout << "RigidHemisphere::compute()" << std::endl;
      if(f0) _energy = 0.0;

      if(f1) { 
	_FZ = 0.0;
	_forces = Vector3D(0.0);
      }

      _penetration = 0.0;
 
      for(int a=0; a<_defNodes.size(); a++) {
	
	DefNode * nd = _defNodes[a];
	Vector3D x;
	x = nd->point();
	
	// current normal
	Vector3D n;
	n = x-_xc;
	double R = tvmet::norm2(n);
	n /= R;

	// Do normal contact first
	
	// normal gap function
	double gn = R - _R;
	_penetration = std::max(_penetration, -gn);

	// pressure multiplier and force
	double pn = _pressureMultipliers(a);
	double fn = pn + _k*gn;
	if( fn > 0 ) { // case 1: no contact 
	  if(f0) {
	    _energy -= 0.5*sqr(pn)/_k; 
	  }	  
	  
	} else { // case 2: contact

	  if(f0) {
	    _energy += (pn+0.5*_k*gn)*gn; 
	  }
	  if(f1) {
	    _forces(a) += fn*n;
	  }
	}
	
	// now frictional contact 

	// previous contact point = xc + radius times previous normal

	// compute vector from previous contact point to current point
	Vector3D dx;
	dx = x - ( _xc + _R*_normals(a) );

	// tangential gap, (I-n^t n)*dx
	Vector3D gt;
	gt = dx - dot(_normals(a),dx)*_normals(a);

	// friction multiplier and force
	const Vector3D pt = _frictionMultipliers(a);
	double norm_pt = norm2(pt);

	Vector3D ft; 
	ft = pt + _k*gt;
	
	double norm_ft = norm2(ft);

	if( pn > 0 ) { // case 1: no contact 
	  if(f0) {
	    _energy -= 0.5*sqr(norm_pt)/_k; 
	  }	  
	  
	} else if( norm_ft <= _mu*std::abs(pn) ) { // case 2: contact, stick

	  if(f0) {
	    _energy += dot(pt+0.5*_k*gt, gt); 
	  }
	  if(f1) {
	    _forces(a) += ft - dot(ft, _normals(a))*_normals(a);
	  }

	} else { // case 3: contact, slip
	  
	  if(f0) {
	    _energy -= 
	      0.5*( sqr(norm_pt) - 2.0*_mu*std::abs(pn)*norm_ft + sqr(_mu*pn) )/_k; 
	  }
	  if(f1) {
	    _forces(a) += 
	      _mu*std::abs(pn)*( ft-dot(ft,_normals(a))*_normals(a) )/norm_ft;
	  }

	}

	if(f1) {
	  const Vector3D & f = _forces(a); 
	  _FZ += f(2);
	  for(int i=0; i<3; i++) nd->addForce(i,f(i)); 
	}

      }
    }
  private:

    double _k;
    double _R;
    double _FZ;
    double _mu;
    Vector3D _xc;
    DefNodeContainer _defNodes;

    std::vector<bool> _active;
    blitz::Array<Vector3D,1> _forces;
    blitz::Array<double,1>   _pressureMultipliers;
    blitz::Array<Vector3D,1> _frictionMultipliers;
    blitz::Array<Vector3D,1> _normals;

  };

  class RigidPlateAL : public RigidSurface
  {
  public:

    // typedefs
    typedef tvmet::Vector<double,3> Vector3D;
    typedef DeformationNode<3> DefNode;
    typedef std::vector< DefNode* > DefNodeContainer;
    typedef DefNodeContainer::iterator DefNodeIterator;
    typedef DefNodeContainer::const_iterator ConstDefNodeIterator;
    typedef std::map< DefNode*, double > ActiveSet;
    typedef ActiveSet::iterator ActiveSetIterator;
    typedef ActiveSet::const_iterator ConstActiveSetIterator;

    RigidPlateAL( const DefNodeContainer & nodes, double k, double Z, 
		  bool up=true, double friction=0.0 ) {
      _defNodes = nodes;
      _active.resize(nodes.size());
      for(ConstDefNodeIterator n=nodes.begin(); n!=nodes.end(); n++) {
	_baseNodes.push_back(*n);
      }
      _k = k;
      _Z = Z;
      _FZ = 0.0;
      _up = up;
      if(_up) {
	_normal = 0.0, 0.0, 1.0;
      } else {
	_normal = 0.0, 0.0, -1.0;
      }

      _penetration = 0.0;

      _mu = friction;
      _penetration = 0.0;
      _active.resize(nodes.size());
      _x_stick.resize(_defNodes.size());
      _x_stick = Vector3D(0.0);
      _forces.resize(_defNodes.size());
      _forces = Vector3D(0.0);
      _pressureMultipliers.resize(_defNodes.size());
      _pressureMultipliers = 0.0;
      _frictionMultipliers.resize(_defNodes.size());
      _frictionMultipliers = Vector3D(0.0);
      updateContact();
    }


    double penaltyCoefficient() const {return _k;}

    void setPenaltyCoefficient( double k ) { _k = k; }

    int active() const { 
      int n=0; 
      for(int i=0; i<_active.size(); i++) {
	if (_active[i]) n++;
      }
      return n; 
    }


    void updateContact() {
      
      // Now update surface normals and check for
      // active contacts
      for(int a=0; a<_defNodes.size(); a++) {
	const Vector3D & x = _defNodes[a]->point();
	double z = x(2) ;
	
	// save projection of current point on surface as frictional
	// ``stick'' point
	_x_stick(a) = x;
	_x_stick(a)(2) = _Z;

	// check for active contacts
 	if( (_up && z < _Z) || (!_up && z > _Z) ) {	
 	  _active[a] = true;
	} else {
	  _active[a] = false;
	}
	
	// Uzawa update: set multipliers equal to most recently computed
	// forces
	_pressureMultipliers(a) = dot(_forces(a), _normal);
	_frictionMultipliers(a) = _forces(a) 
	  			- _pressureMultipliers(a)*_normal;
	

      }


      return;
    }

    void setZ(double Z) { _Z = Z; }

    double FZ() const {return _FZ;}

    double penetration() const {return _penetration;}

    // E = 0.5 * k * ( x3-Z )^2
    // f3 = dE/dx3 = k * ( x3-Z )
    //! Do mechanics on Body
    virtual void compute( bool f0, bool f1, bool f2 ) {
      //std::cout << "RigidPlateAL::compute()" << std::endl;

      if(f0) _energy = 0.0;
      
      if(f1) {
	_FZ = 0.0;
	_forces = Vector3D(0.0);
      }

      _penetration = 0.0;


      for(int a=0; a<_defNodes.size(); a++) {
	
	DefNode * nd = _defNodes[a];
	Vector3D x;
	x = nd->point();
	
	// Do normal contact first
	
	// normal gap function
	double gn = dot(x - _x_stick(a), _normal);
	_penetration = std::max(_penetration, -gn);

	// pressure multiplier and force
	double pn = _pressureMultipliers(a);
	double fn = pn + _k*gn;
	if( fn > 0 ) { // case 1: no contact 
	  if(f0) {
	    _energy -= 0.5*sqr(pn)/_k; 
	  }	  
	  
	} else { // case 2: contact

	  if(f0) {
	    _energy += (pn+0.5*_k*gn)*gn; 
	  }
	  if(f1) {
	    _forces(a) += fn*_normal;
	  }
	}
	
	// now frictional contact 

	// previous contact point = xc + radius times previous normal

	// compute vector from previous contact point to current point
	Vector3D dx;
	dx = x - _x_stick(a);

	// tangential gap, (I-n^t n)*dx
	Vector3D gt;
	gt = dx - dot(_normal,dx)*_normal;

	// friction multiplier and force
	const Vector3D pt = _frictionMultipliers(a);
	double norm_pt = norm2(pt);

	Vector3D ft; 
	ft = pt + _k*gt;
	
	double norm_ft = norm2(ft);

	if( pn > 0 ) { // case 1: no contact 
	  if(f0) {
	    _energy -= 0.5*sqr(norm_pt)/_k; 
	  }	  
	  
	} else if( norm_ft <= _mu*std::abs(pn) ) { // case 2: contact, stick

	  if(f0) {
	    _energy += dot(pt+0.5*_k*gt, gt); 
	  }
	  if(f1) {
	    _forces(a) += ft - dot(ft, _normal)*_normal;
	  }

	} else { // case 3: contact, slip
	  
	  if(f0) {
	    _energy -= 
	      0.5*( sqr(norm_pt) - 2.0*_mu*std::abs(pn)*norm_ft + sqr(_mu*pn) )/_k; 
	  }
	  if(f1) {
	    _forces(a) += 
	      _mu*std::abs(pn)*( ft-dot(ft,_normal)*_normal )/norm_ft;
	  }

	}

	if(f1) {
	  const Vector3D & f = _forces(a); 
	  _FZ += f(2);
	  for(int i=0; i<3; i++) nd->addForce(i,f(i)); 
	}

      }
 
    }
  private:

    double _k;
    double _Z;
    double _FZ;
    DefNodeContainer _defNodes;
    bool _up;

    Vector3D _normal;

//     ActiveSet _active;
    std::vector<bool> _active;

    double _mu;
    blitz::Array<Vector3D,1> _x_stick;
    blitz::Array<Vector3D,1> _forces;
    blitz::Array<double,1>   _pressureMultipliers;
    blitz::Array<Vector3D,1> _frictionMultipliers;
  };

  class PenaltyBC : public Element
  {
  public:

    // typedefs
    typedef tvmet::Vector<double,3> Vector3D;
    typedef tvmet::Vector<bool,3> VectorBC;
    typedef DeformationNode<3> DefNode;

    PenaltyBC( DefNode * node, Vector3D x0, VectorBC bc, double k ) {
      _defNode = node;
      _baseNodes.push_back(node);

      _k = k;
      _dx = 0.0;
      _x0 = x0;
      _bc = bc;
      _f = 0.0, 0.0, 0.0;
    }

    double penetration() const {return _dx;}

    double penaltyCoefficient() const {return _k;}

    void setPenaltyCoefficient( double k ) { _k = k; }

    // E = 0.5 * k * ( x3-Z )^2
    // f3 = dE/dx3 = k * ( x3-Z )
    //! Do mechanics on Body
    virtual void compute( bool f0, bool f1, bool f2 ) {
      //std::cout << "RigidBC::compute()" << std::endl;

      if(f0) _energy = 0.0;

      Vector3D dx(0.0);
      for(int i=0; i<3; i++) {
	if(_bc(i)) {
	  dx(i) = _defNode->getPoint(i) - _x0(i);
	  _dx = std::max(std::abs(dx(i)), _dx);
	}
      }

      if(f0) {
	_energy += 0.5*_k*tvmet::dot(dx,dx);
      }
      if(f1) {
	_f = 0.0, 0.0, 0.0;
	for(int i=0; i<3; i++) {
	  if(_bc(i)) {
	    _f(i) = _k*dx(i);
	    _defNode->addForce(i, _f(i));
	  }
	}
      }
    }

    double force(int i) const {return _f(i);}

  private:

    double _k;
    double _dx;

    Vector3D _x0;
    Vector3D _f;
    VectorBC _bc;
    DefNode * _defNode;

  };


} // end namespace voom