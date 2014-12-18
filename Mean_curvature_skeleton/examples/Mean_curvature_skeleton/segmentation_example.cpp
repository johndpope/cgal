#include <CGAL/boost/graph/graph_traits_Polyhedron_3.h>
#include <CGAL/Polyhedron_items_with_id_3.h>
#include <CGAL/Simple_cartesian.h>
#include <CGAL/Eigen_solver_traits.h>
#include <CGAL/Mean_curvature_skeleton.h>
#include <CGAL/internal/corefinement/Polyhedron_subset_extraction.h>
#include <CGAL/IO/Polyhedron_iostream.h>
#include <CGAL/mesh_segmentation.h>

#include <boost/property_map/property_map.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/iterator/transform_iterator.hpp>

#include <fstream>
#include <map>

typedef CGAL::Simple_cartesian<double>                               Kernel;
typedef Kernel::Point_3                                              Point;
typedef Kernel::Vector_3                                             Vector;
typedef CGAL::Polyhedron_3<Kernel, CGAL::Polyhedron_items_with_id_3> Polyhedron;

typedef Polyhedron::Facet_iterator                                   Facet_iterator;
typedef boost::graph_traits<Polyhedron>::vertex_descriptor           vertex_descriptor;
typedef boost::graph_traits<Polyhedron>::vertex_iterator             vertex_iterator;
typedef boost::graph_traits<Polyhedron>::halfedge_descriptor         halfedge_descriptor;

struct Skeleton_vertex_info
{
  std::size_t id;
};

typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::undirectedS, Skeleton_vertex_info> SkeletonGraph;

typedef boost::graph_traits<SkeletonGraph>::vertex_descriptor          vertex_desc;
typedef boost::graph_traits<SkeletonGraph>::vertex_iterator            vertex_iter;
typedef boost::graph_traits<SkeletonGraph>::edge_iterator              edge_iter;

typedef boost::property_map<Polyhedron, boost::vertex_index_t>::type     Vertex_index_map;
typedef boost::property_map<Polyhedron, boost::halfedge_index_t>::type   Edge_index_map;

typedef std::map<vertex_desc, std::vector<int> >                       Correspondence_map;
typedef boost::associative_property_map<Correspondence_map>            GraphVerticesPMap;

typedef CGAL::MCF_default_halfedge_graph_pmap<Polyhedron>::type        HalfedgeGraphPointPMap;

typedef std::map<vertex_desc, Point>                                   GraphPointMap;
typedef boost::associative_property_map<GraphPointMap>                 GraphPointPMap;

// The input of the skeletonization algorithm must be a pure triangular closed
// mesh and has only one component.
bool is_mesh_valid(Polyhedron& pMesh)
{
  if (!pMesh.is_closed())
  {
    std::cerr << "The mesh is not closed.";
    return false;
  }
  if (!pMesh.is_pure_triangle())
  {
    std::cerr << "The mesh is not a pure triangle mesh.";
    return false;
  }

  // the algorithm is only applicable on a mesh
  // that has only one connected component
  std::size_t num_component;
  CGAL::Counting_output_iterator output_it(&num_component);
  CGAL::internal::extract_connected_components(pMesh, output_it);
  ++output_it;
  if (num_component != 1)
  {
    std::cerr << "The mesh is not a single closed mesh. It has " 
              << num_component << " components.";
    return false;
  }
  return true;
}

int main()
{
  Polyhedron mesh;
  std::ifstream input("data/161.off");

  if ( !input || !(input >> mesh) || mesh.empty() ) {
    std::cerr << "Cannot open data/161.off" << std::endl;
    return 1;
  }
  if (!is_mesh_valid(mesh)) {
    return 1;
  }

  SkeletonGraph g;
  GraphPointMap points_map;
  GraphPointPMap points(points_map);

  Correspondence_map corr_map;
  GraphVerticesPMap corr(corr_map);

  CGAL::MCF_skel_args<Polyhedron> skeleton_args(mesh);
  
  CGAL::extract_skeleton(
      mesh, Vertex_index_map(), Edge_index_map(),
      skeleton_args, g, points, corr);

  // add segmentation
  vertex_iterator vb, ve;
  std::vector<vertex_descriptor> id_to_vd;
  id_to_vd.clear();
  id_to_vd.resize(boost::num_vertices(mesh));
  for (boost::tie(vb, ve) = boost::vertices(mesh); vb != ve; ++vb)
  {
    vertex_descriptor v = *vb;
    id_to_vd[v->id()] = v;
  }

  // create a property-map for sdf values (it is an adaptor for this case)
  typedef std::map<Polyhedron::Facet_const_handle, double> Facet_double_map;
  Facet_double_map internal_sdf_map;
  boost::associative_property_map<Facet_double_map> sdf_property_map(internal_sdf_map);

  std::vector<double> distances;
  distances.resize(boost::num_vertices(mesh));

  vertex_iter gvb, gve;
  for (boost::tie(gvb, gve) = boost::vertices(g); gvb != gve; ++gvb)
  {
    vertex_desc i = *gvb;
    Point skel = points[i];
    for (size_t j = 0; j < corr[i].size(); ++j)
    {
      Point surf = id_to_vd[corr[i][j]]->point();
      distances[corr[i][j]] = sqrt(squared_distance(skel, surf));
    }
  }

  // compute sdf values with skeleton
  for (Facet_iterator f = mesh.facets_begin(); f != mesh.facets_end(); ++f)
  {
    Polyhedron::Halfedge_const_handle he = f->facet_begin();
    int vid1 = he->vertex()->id();
    int vid2 = he->next()->vertex()->id();
    int vid3 = he->next()->next()->vertex()->id();
    double dis1 = distances[vid1];
    double dis2 = distances[vid2];
    double dis3 = distances[vid3];
    double avg_dis = (dis1 + dis2 + dis3) / 3.0;
    sdf_property_map[f] = avg_dis;
  }

  CGAL::sdf_values_postprocessing(mesh, sdf_property_map);

  // create a property-map for segment-ids (it is an adaptor for this case)
  typedef std::map<Polyhedron::Facet_const_handle, int> Facet_int_map;
  Facet_int_map internal_segment_map;
  boost::associative_property_map<Facet_int_map> segment_property_map(internal_segment_map);

  // segment the mesh using default parameters for number of levels, and smoothing lambda
  // Note that you can use your own scalar value, instead of using SDF calculation computed using the CGAL function
  int number_of_segments = CGAL::segmentation_from_sdf_values(mesh, sdf_property_map, segment_property_map);
  std::cout << "Number of segments: " << number_of_segments << std::endl;

  return 0;
}

