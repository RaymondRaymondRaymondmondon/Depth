#pragma once
// The Study's course packs (Master Reference stage 17): loading a pack from study/courses/<id>/pack for the Courses
// drawer and the problem engine, and the headless tools Claude Code's ingestion (study/INGEST.md) runs:
//   depth.exe --course-verify [course] [unit]   the automatic gates (1 computation, 2 blind-solve comparison,
//                                               3 grounding, 5 consistency, 6 tier audit) + structure + coverage
//   depth.exe --course-report [course]          per-unit readiness
//   depth.exe --course-expand [course] [unit]   templates -> verified instances (instances/Uxx.json)
//   depth.exe --course-seed-test                the seeded-error fixture: every planted error must be caught
// No raylib here.
#include <string>
#include <vector>
#include "json.h"

struct CourseSkill { std::string id, name; };
struct CourseUnit {
    std::string id, title, type, status;   // status: planned / building / built
    std::vector<CourseSkill> skills;
    int items = 0, quarantined = 0;
};
struct Course {
    std::string id, code, title, term, dir;   // dir = the course folder (holding source/ and pack/)
    int version = 0;
    std::vector<CourseUnit> units;
};

std::string FindCoursesRoot();                                // study/courses next to the cwd or the exe, or ""
std::vector<Course> LoadCourses(const std::string& root);     // every pack that has a readable course.json
bool LoadCourse(const std::string& dir, Course& out, std::string* err = nullptr);

int RunCourseVerify(int argc, char** argv, int i);            // i = index after the flag; returns an exit code
int RunCourseReport(int argc, char** argv, int i);
int RunCourseExpand(int argc, char** argv, int i);
int RunCourseSeedTest(int argc, char** argv, int i);
