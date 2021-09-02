#include "sv/node/viz.h"

#include <glog/logging.h>
#include <pcl_conversions/pcl_conversions.h>
#include <tbb/parallel_for.h>
#include <tf2_eigen/tf2_eigen.h>

#include <Eigen/Eigenvalues>
#include <Eigen/Geometry>

namespace sv {

using visualization_msgs::Marker;
using visualization_msgs::MarkerArray;

void MeanCovar2Marker(const Eigen::Vector3d& mean,
                      Eigen::Vector3d eigvals,
                      Eigen::Matrix3d eigvecs,
                      Marker& marker) {
  MakeRightHanded(eigvals, eigvecs);
  const Eigen::Quaterniond quat(eigvecs);
  eigvals = eigvals.cwiseSqrt() * 2;

  marker.pose.position.x = mean.x();
  marker.pose.position.y = mean.y();
  marker.pose.position.z = mean.z();
  marker.pose.orientation.w = quat.w();
  marker.pose.orientation.x = quat.x();
  marker.pose.orientation.y = quat.y();
  marker.pose.orientation.z = quat.z();
  marker.scale.x = eigvals.x();
  marker.scale.y = eigvals.y();
  marker.scale.z = eigvals.z();
}

void Grid2Markers(const SweepGrid& grid,
                  const std_msgs::Header& header,
                  std::vector<Marker>& markers) {
  const double alpha = 0.5;
  const double eps = 1e-8;

  markers.resize(grid.total() * 2 + 1);

  Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> es;
  auto& line_mk = markers.back();
  line_mk.header = header;
  line_mk.ns = "match";
  line_mk.id = 0;
  line_mk.type = Marker::LINE_LIST;
  line_mk.action = Marker::ADD;
  line_mk.color.a = 1.0;
  line_mk.color.b = 1.0;
  line_mk.points.clear();
  line_mk.points.reserve(grid.total() * 2);
  line_mk.scale.x = 0.005;
  line_mk.pose.orientation.w = 1.0;

  for (int r = 0; r < grid.rows(); ++r) {
    for (int c = 0; c < grid.cols(); ++c) {
      const auto i = grid.Px2Ind({c, r});
      const auto& match = grid.MatchAt({c, r});

      auto& pano_mk = markers.at(i);
      auto& grid_mk = markers.at(i + grid.total());

      pano_mk.header = header;
      pano_mk.ns = "pano";
      pano_mk.id = i;
      pano_mk.type = Marker::SPHERE;
      pano_mk.color.g = 1.0;

      grid_mk.header = header;
      grid_mk.ns = "grid";
      grid_mk.id = i;
      grid_mk.type = Marker::SPHERE;
      grid_mk.color.a = alpha;
      grid_mk.color.r = 1.0;

      if (match.Ok()) {
        pano_mk.action = Marker::ADD;
        pano_mk.color.a = match.scale;  // use scale for alpha
        const auto pt_p = match.mc_p.mean.cast<double>().eval();
        auto pano_cov = match.mc_p.Covar().cast<double>().eval();
        pano_cov.diagonal().array() += eps;
        es.compute(pano_cov);
        MeanCovar2Marker(pt_p, es.eigenvalues(), es.eigenvectors(), pano_mk);

        grid_mk.action = Marker::ADD;
        const auto pt_g =
            (grid.TfAt(c) * match.mc_g.mean).cast<double>().eval();
        auto grid_cov = match.mc_g.Covar().cast<double>().eval();
        grid_cov.diagonal().array() += eps;
        es.compute(grid_cov);
        MeanCovar2Marker(pt_g, es.eigenvalues(), es.eigenvectors(), grid_mk);

        // Line
        geometry_msgs::Point p0, p1;
        p0.x = pt_p.x();
        p0.y = pt_p.y();
        p0.z = pt_p.z();
        p1.x = pt_g.x();
        p1.y = pt_g.y();
        p1.z = pt_g.z();
        line_mk.points.push_back(p0);
        line_mk.points.push_back(p1);
      } else {
        pano_mk.action = Marker::DELETE;
        grid_mk.action = Marker::DELETE;
      }
    }
  }
}

cv::Mat ApplyCmap(const cv::Mat& input,
                  double scale,
                  int cmap,
                  uint8_t bad_color) {
  CHECK_EQ(input.channels(), 1);

  cv::Mat disp;
  input.convertTo(disp, CV_8UC1, scale * 255.0);
  cv::applyColorMap(disp, disp, cmap);

  if (input.depth() >= CV_32F) {
    disp.setTo(bad_color, cv::Mat(~(input > 0)));
  }

  return disp;
}

void Imshow(const std::string& name, const cv::Mat& mat, int flag) {
  cv::namedWindow(name, flag);
  cv::imshow(name, mat);
  cv::waitKey(1);
}

void Pano2Cloud(const DepthPano& pano,
                const std_msgs::Header header,
                CloudXYZ& cloud) {
  const auto size = pano.size();
  if (cloud.empty()) {
    cloud.resize(pano.total());
    cloud.width = size.width;
    cloud.height = size.height;
  }

  pcl_conversions::toPCL(header, cloud.header);
  tbb::parallel_for(tbb::blocked_range<int>(0, size.height),
                    [&](const auto& blk) {
                      for (int r = blk.begin(); r < blk.end(); ++r) {
                        for (int c = 0; c < size.width; ++c) {
                          const auto rg = pano.RangeAt({c, r});
                          auto& pc = cloud.at(c, r);
                          if (rg == 0) {
                            pc.x = pc.y = pc.z = kNaNF;
                          } else {
                            const auto pp = pano.model.Backward(r, c, rg);
                            pc.x = pp.x;
                            pc.y = pp.y;
                            pc.z = pp.z;
                          }
                        }
                      }
                    });
}

void Sweep2Cloud(const LidarSweep& sweep,
                 const std_msgs::Header header,
                 CloudXYZI& cloud) {
  const auto size = sweep.size();
  if (cloud.empty()) {
    cloud.resize(sweep.total());
    cloud.width = size.width;
    cloud.height = size.height;
  }

  pcl_conversions::toPCL(header, cloud.header);
  tbb::parallel_for(tbb::blocked_range<int>(0, size.height),
                    [&](const auto& blk) {
                      for (int r = blk.begin(); r < blk.end(); ++r) {
                        for (int c = 0; c < size.width; ++c) {
                          const bool col_in_curr =
                              (sweep.curr.start <= c && c < sweep.curr.end);
                          float intensity = col_in_curr ? 1.0 : 0.5;

                          const auto& tf = sweep.TfAt(c);
                          const auto& xyzr = sweep.XyzrAt({c, r});
                          auto& pc = cloud.at(c, r);
                          if (std::isnan(xyzr[0])) {
                            pc.x = pc.y = pc.z = pc.intensity = kNaNF;
                          } else {
                            Eigen::Map<const Eigen::Vector3f> xyz(&xyzr[0]);
                            pc.getArray3fMap() = tf * xyz;
                            pc.intensity = intensity;
                          }
                        }
                      }
                    });
}

void Traj2PoseArray(const Trajectory& traj, geometry_msgs::PoseArray& parray) {
  parray.poses.resize(traj.size());
  for (int i = 0; i < traj.size(); ++i) {
    const auto& st = traj.At(i);
    auto& pose = parray.poses.at(i);
    const auto T_p_l = Sophus::SE3d(st.rot, st.pos) * traj.T_imu_lidar;
    pose.orientation = tf2::toMsg(T_p_l.unit_quaternion());
    pose.position = tf2::toMsg(T_p_l.translation());
  }
}

}  // namespace sv
