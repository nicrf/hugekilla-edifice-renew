package main

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
)

func TestVersion(t *testing.T) {
	version, commit = "1.2.3", "0123456789abcdef"
	defer func() { version, commit = "dev", "" }()
	api := &API{}
	rec := httptest.NewRecorder()
	api.ServeHTTP(rec, httptest.NewRequest(http.MethodGet, "/v1/version", nil))
	var got struct {
		Version, Commit string
		API             int
	}
	if rec.Code != http.StatusOK || json.Unmarshal(rec.Body.Bytes(), &got) != nil {
		t.Fatalf("GET /v1/version: %d %s", rec.Code, rec.Body)
	}
	if got.Version != "1.2.3" || got.Commit != commit || got.API != APIVersion {
		t.Fatalf("GET /v1/version = %+v", got)
	}
	if s := versionString(); s != "edifice-lobby 1.2.3 (0123456)" {
		t.Fatalf("versionString() = %q", s)
	}
}
